#include "fat32impl.h"

#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>

struct _diskParams_t diskParams;

/* function for abnormal program termination*/
extern void (*errExit)(char* );

static uint32_t genVolumeID()
{
    time_t t;
    struct tm* tm_info;
    time(&t);
    tm_info = localtime(&t);

    int year = tm_info->tm_year + 1900;
    int month = tm_info->tm_mon + 1;
    int day = tm_info->tm_mday;
    int hour = tm_info->tm_hour;
    int minute = tm_info->tm_min;

    uint32_t high = (year << 5) | (minute << 8) | (month << 5) | day;
    uint32_t low = (hour << 8) | (minute << 5) | minute;

    return (high << 16) | low;
}

static int calcClSizePower(uint32_t cluster_size)
{
    for (uint8_t power = 8; power <= 64; ++power) {
        if (cluster_size == (1 << power))
            return power;
    }
    perror("Incorrect cluster size");
    return -1;
}

void initBootSector()
{
    bootSector_t bs;
    memset(&bs, 0, sizeof(bs));
    bs.BS_jumpBoot[0] = 0xEB;
    bs.BS_jumpBoot[1] = 0x5A;
    bs.BS_jumpBoot[2] = 0x90;
    strncpy((char*)bs.BS_OEMName, "MSWIN4.1", 8);
    bs.BPB_BytesPerSec = SECTOR_SIZE;

    bs.BPB_SecPerClus = DEFAULT_SECT_PER_CLUS;
    /* Cluster size should be selected according to this table from microsoft spec,
     * but we set our own cluster size
    DSKSZTOSECPERCLUS DskTableFAT32 [] = {
            { 66600, 0},        // disks up to 32.5 MB, the 0 value for SecPerClusVal trips an error
            { 532480, 1},       // disks up to 260 MB, .5k cluster
            { 16777216, 8},     // disks up to 8 GB, 4k cluster
            { 33554432, 16},    // disks up to 16 GB, 8k cluster
            { 67108864, 32},    // disks up to 32 GB, 16k cluster
            { 0xFFFFFFFF, 64}   // disks greater than 32GB, 32k cluster
    }; */

    bs.BPB_RsvdSecCnt = 32;
    bs.BPB_NumFATs = 2;
    bs.BPB_Media = 0xF8;
    bs.BPB_TotSec32 = diskParams.volume_size / SECTOR_SIZE;

    /* FAT table size calculation according to microsoft spec: */
    uint32_t tmpVal1 = bs.BPB_TotSec32 - bs.BPB_RsvdSecCnt;
    uint32_t tmpVal2 = ((bs.BPB_SecPerClus << 8) + bs.BPB_NumFATs) >> 1;
    bs.BPB_FATSz32 = (tmpVal1 + (tmpVal2 - 1)) / tmpVal2;

    // bs.BPB_ExtFlags = 0x40;
    bs.BPB_RootClus = 2;
    bs.BPB_FSInfo = 1;
    bs.BPB_BkBootSec = DEFAULT_BACKUP_SECTOR;
    bs.BS_DrvNum = 0x80;
    bs.BS_BootSig = 0x29;
    bs.BS_VolID = genVolumeID();
    strncpy((char*)bs.BS_VolLab, diskParams.volume_label, 11);
    strncpy((char*)bs.BS_FilSysType, "FAT32   ", 8);
    *(uint16_t*)&bs.offset[510] = 0xAA55;

    ssize_t num_write = pwrite(diskParams.fd, &bs, SECTOR_SIZE, 0);
    if (num_write != SECTOR_SIZE)
        errExit("pwrite bs");

    /* backup copy */
    num_write = pwrite(diskParams.fd, &bs, SECTOR_SIZE, SECTOR_SIZE * DEFAULT_BACKUP_SECTOR);
    if (num_write != SECTOR_SIZE)
        errExit("pwrite bs");

    diskParams.FAT_size = bs.BPB_FATSz32;
    diskParams.data_offset = (bs.BPB_RsvdSecCnt + bs.BPB_NumFATs * bs.BPB_FATSz32) * SECTOR_SIZE;
    diskParams.cluster_size_bytes = bs.BPB_SecPerClus * SECTOR_SIZE;
    diskParams.reserved_sectors = bs.BPB_RsvdSecCnt;
}

void initFSInfoSector()
{
    FSInfoSector_t fsi;
    memset(&fsi, 0, sizeof(fsi));
    fsi.FSI_LeadSig = 0x41615252;
    fsi.FSI_StrucSig = 0x61417272;
    fsi.FSI_Free_Count = 0xFFFFFFFF;
    fsi.FSI_Nxt_Free = 0xFFFFFFFF;
    fsi.FSI_TrailSig = 0xAA550000;

    ssize_t num_write = pwrite(diskParams.fd, &fsi, SECTOR_SIZE, SECTOR_SIZE);
    if (num_write != SECTOR_SIZE)
        errExit("pwrite fsi1");
    
    /* backup copy */
    num_write = pwrite(diskParams.fd, &fsi, SECTOR_SIZE, SECTOR_SIZE * (DEFAULT_BACKUP_SECTOR + 1));
    if (num_write != SECTOR_SIZE)
        errExit("pwrite fsi2");
}

void initFATTables()
{
    uint32_t firstEntries[2] = {0x0FFFFFF8, 0x0FFFFFFF};

    off_t offset = diskParams.reserved_sectors * SECTOR_SIZE;
    ssize_t num_write = pwrite(diskParams.fd, &firstEntries, 8, offset);
    if (num_write == -1)
        errExit("pwrite fat1");

    offset = (diskParams.reserved_sectors + diskParams.FAT_size) * SECTOR_SIZE;
    num_write = pwrite(diskParams.fd, &firstEntries, 8, offset);
    if (num_write == -1)
        errExit("pwrite fat2");
}

void initRoot()
{
    void* cluster = calloc(diskParams.cluster_size_bytes, 1);
    if (cluster == NULL)
        errExit("malloc");
    directory_t* dir = (directory_t*)cluster;
    strncpy(dir->DIR_Name, "/          ", 11);
    dir->DIR_Attr = 0x30;
    uint32_t clus_number = 0x02;
    dir->DIR_FstClusHI = clus_number & 0xF0;
    dir->DIR_FstClusLO = clus_number & 0x0F;
    
    /* write to cluster */
    uint32_t FAT_offset = clus_number;
    uint32_t ThisFATSecNum = diskParams.reserved_sectors + (FAT_offset / 128);
    uint32_t ThisFATEntOffset = FAT_offset % 128;
    uint32_t fat_value = 0x0FFFFFFF;
    off_t offset = ThisFATSecNum * SECTOR_SIZE + ThisFATEntOffset * 4;

    ssize_t num_write = pwrite(diskParams.fd, &fat_value, 4, offset);
    if (num_write == -1) {
        free(cluster);
        errExit("pwrite initroot");
    }

    /* write to cluster */
    offset = diskParams.data_offset + clus_number * diskParams.cluster_size_bytes;
    num_write = pwrite(diskParams.fd, cluster, diskParams.cluster_size_bytes, offset);
    if (num_write == -1) {
        free(cluster);
        errExit("pwrite initroot 2");
    }

    free(cluster);
}

bool isFAT32Disk()
{
    bootSector_t bs;
    ssize_t num_read = pread(diskParams.fd, &bs, SECTOR_SIZE, 0);
    if (num_read == -1)
        errExit("read");

    if (num_read < SECTOR_SIZE      ||
        (bs.BPB_BytesPerSec != SECTOR_SIZE) ||
        (bs.BPB_SecPerClus == 0)    ||
        (bs.BPB_NumFATs != 2)       ||
        (bs.BPB_RootClus == 0)      ||
        (bs.BPB_FATSz32 == 0)       ||
        (*(uint16_t*)&bs.offset[510] != 0xAA55)) {
            return false;
        }
    return true;
}

void setDiskParams()
{
    bootSector_t bs;
    ssize_t num_read = pread(diskParams.fd, &bs, SECTOR_SIZE, 0);
    if (num_read == -1)
        errExit("read");

    diskParams.FAT32 = true;
    diskParams.FAT_size = bs.BPB_FATSz32;
    diskParams.root_cluster = bs.BPB_RootClus;
    diskParams.data_offset = (bs.BPB_RsvdSecCnt + bs.BPB_NumFATs * bs.BPB_FATSz32) * SECTOR_SIZE;
    diskParams.cluster_size_bytes = bs.BPB_SecPerClus * SECTOR_SIZE;
    diskParams.cluster_power = calcClSizePower(diskParams.cluster_size_bytes);
    diskParams.active_dir = bs.BPB_RootClus;
    diskParams.reserved_sectors = bs.BPB_RsvdSecCnt;
    diskParams.page_size = sysconf(_SC_PAGESIZE);
    if (diskParams.page_size == -1) {
        perror("sysconf");
        diskParams.page_size = 4096;
    }

    if (diskParams.FSI_sector != NULL && diskParams.FSI_sector != MAP_FAILED) {
        diskParams.FSI_sector -= 1;
        if (msync(diskParams.FSI_sector, SECTOR_SIZE, MS_SYNC) == -1)
            perror("msync");
        if (munmap(diskParams.FSI_sector, SECTOR_SIZE) == -1)
            perror("munmap");
    }
    if (diskParams.FAT_sector != NULL && diskParams.FAT_sector != MAP_FAILED) {
        if (msync(diskParams.FAT_sector, SECTOR_SIZE, MS_SYNC) == -1)
            perror("msync");
        if (munmap(diskParams.FAT_sector, SECTOR_SIZE) == -1)
            perror("munmap");
    }
    diskParams.FSI_sector = NULL;
    diskParams.FAT_sector = NULL;
    diskParams.FAT_sector_number = 0;
}

void mountDisk(char* volume)
{
    mode_t filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH; /* rw-rw-rw- */

    int disk_fd = open(volume, O_RDWR);
    if (disk_fd == -1) {
        if (errno != ENOENT) {
            errExit("open");
        } else {
            disk_fd = open(volume, O_RDWR | O_CREAT, filePerms);
            if (disk_fd == -1)
                errExit("open");
            if (ftruncate(disk_fd, DEFAULT_DISK_SIZE) == -1)
                errExit("ftruncate");
            printf("Disk was created with a size of 20 MB.\n");
        }
    }
    diskParams.fd = disk_fd;

    struct stat statBuf;
    if (fstat(disk_fd, &statBuf) == -1)
        errExit("fstat");

    diskParams.volume_size = statBuf.st_size;
    strncpy(diskParams.volume_label, DEFAULT_VOLUME_LABEL, 11);    
}

int cashFSISector()
{
    if (diskParams.FSI_sector == NULL) {
        diskParams.FSI_sector = (FSInfoSector_t*)mmap(NULL, SECTOR_SIZE << 1, PROT_READ | PROT_WRITE, MAP_SHARED, diskParams.fd, 0);
        if (diskParams.FSI_sector == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        diskParams.FSI_sector += 1;
    }
    return 0;
}

FSInfoSector_t* findFreeCluster(uint32_t* restrict dest)
{
    if (cashFSISector() == -1) {
        errExit("FSI cash error");
    }
    
    /* scan full disk */
    /* in case of a large fat tables, it should be done in parts. */
    bool is_free_cluster_correct = !(diskParams.FSI_sector->FSI_Nxt_Free == 0xFFFFFFFF || diskParams.FSI_sector->FSI_Nxt_Free > (diskParams.FAT_size << 7));
    bool is_free_cluster_number_correct = !(diskParams.FSI_sector->FSI_Free_Count == 0xFFFFFFFF || (diskParams.FSI_sector->FSI_Free_Count > (diskParams.FAT_size << 7)));
    uint32_t free_cluster_number = 0;
    if (!is_free_cluster_number_correct || !is_free_cluster_correct) {
        uint32_t* FAT = (uint32_t*)malloc(diskParams.FAT_size * SECTOR_SIZE);
        if (FAT == NULL) {
            perror("malloc");
            return NULL;
        }
        ssize_t num_read = pread(diskParams.fd, FAT, diskParams.FAT_size * SECTOR_SIZE, diskParams.reserved_sectors * SECTOR_SIZE);
        if (num_read == -1) {
            free(FAT);
            perror("pread");
            return NULL;
        }
        uint32_t count = 0;
        for (uint32_t i = diskParams.root_cluster; i < (diskParams.FAT_size << 7); ++i) {
            if ((FAT[i] & 0x0FFFFFFF) == 0) {
                count++;
                if (!is_free_cluster_correct) {
                    is_free_cluster_correct = true;
                    free_cluster_number = i;
                    diskParams.FSI_sector->FSI_Nxt_Free = i;
                }
            }
        }
        // printf("count: %d", count);
        diskParams.FSI_sector->FSI_Free_Count = count;
        free(FAT);
    }
    
    if (diskParams.FSI_sector->FSI_Free_Count == 0) {
        return NULL;
    }

    /* The disk hasn't just been scanned */
    if (free_cluster_number == 0) {
        uint32_t FAT_offset = diskParams.FSI_sector->FSI_Nxt_Free;
        uint32_t ThisFATSecNum = diskParams.reserved_sectors + (FAT_offset >> 7);
        uint32_t ThisFATEntOffset = FAT_offset % 128;

        /* search in the cached sector */
        if (diskParams.FAT_sector != NULL && diskParams.FAT_sector_number == ThisFATSecNum) {
            for (; ThisFATEntOffset < 128; ++ThisFATEntOffset, ++FAT_offset) {
                if ((diskParams.FAT_sector[ThisFATEntOffset] & 0x0FFFFFFF) == 0) {
                    free_cluster_number = FAT_offset;
                    break;
                }
            }
        }

        if (free_cluster_number == 0) {
            uint32_t* FAT_table = malloc(SECTOR_SIZE);
            if (FAT_table == NULL) {
                perror("pread");
                return NULL;
            }
            uint32_t initial_sector = ThisFATSecNum;
            /* search to the end of the disk */
            for (; ThisFATSecNum < (diskParams.FAT_size + diskParams.reserved_sectors); ++ThisFATSecNum) {
                ssize_t num_read = pread(diskParams.fd, FAT_table, SECTOR_SIZE, ThisFATSecNum * SECTOR_SIZE);
                if (num_read == -1) {
                    free(FAT_table);
                    perror("pread");
                    return NULL;
                }
                for (ThisFATEntOffset = 0; ThisFATEntOffset < 128; ++ThisFATEntOffset) {
                    if ((FAT_table[ThisFATEntOffset] & 0x0FFFFFFF) == 0) {
                        FAT_offset = ((ThisFATSecNum - diskParams.reserved_sectors) << 7) + ThisFATEntOffset;
                        free_cluster_number = FAT_offset;
                        free(FAT_table);
                        goto end;
                    }
                }
            }
            /* search from the start of the disk*/
            for (ThisFATSecNum = diskParams.reserved_sectors; ThisFATSecNum < initial_sector; ++ThisFATSecNum) {
                ssize_t num_read = pread(diskParams.fd, FAT_table, SECTOR_SIZE, ThisFATSecNum * SECTOR_SIZE);
                if (num_read == -1) {
                    free(FAT_table);
                    perror("pread");
                    return NULL;
                }
                for (ThisFATEntOffset = 0; ThisFATEntOffset < 128; ++ThisFATEntOffset) {
                    if ((FAT_table[ThisFATEntOffset] & 0x0FFFFFFF) == 0) {
                        FAT_offset = ((ThisFATSecNum - diskParams.reserved_sectors) << 7) + ThisFATEntOffset;
                        free_cluster_number = FAT_offset;
                        printf("free cluster from 'from start' part: %d\n", free_cluster_number);
                        free(FAT_table);
                        goto end;
                    }
                }
            }
            free(FAT_table);
            return NULL;
        }
    }

    end: ;
    *dest = free_cluster_number;
    return diskParams.FSI_sector;
}