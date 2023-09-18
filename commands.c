#include "commands.h"
#include "fat32impl.h"
#include "tools.h"

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

extern char CWD[INPUT_SIZE];
extern struct _diskParams_t diskParams;
/* function for abnormal program termination*/
extern void (*errExit)(char* );


void performFormat(char* restrict args)
{
    /* if it was a large disk - we should mapping with smaller portions of the file */
    char* file_data = mmap(NULL, diskParams.volume_size, PROT_READ | PROT_WRITE, MAP_SHARED, diskParams.fd, 0);
    if (file_data == MAP_FAILED)
        errExit("mmap");

    memset(file_data, 0, diskParams.volume_size);

    if (msync(file_data, diskParams.volume_size, MS_SYNC) == -1) {
        errExit("msync");
    }
    if (munmap(file_data, diskParams.volume_size) == -1) {
        errExit("munmap");
    }

    initBootSector();
    initFSInfoSector();
    initFATTables();
    initRoot();
    setDiskParams();
    printf("Successfully formatted\n");
}

void performUpdate(char* restrict args)
{
    printf("Volume scan..\n");
    uint32_t free_cluster_number;
    findFreeCluster(&free_cluster_number);
    printf("Updated\n");
}

void performDisk(char* restrict args)
{
    if (cashFSISector() == -1) {
        errExit("FSI cash error");
    }
    
    /* scan full disk */
    /* in case of a large fat tables, it should be done in parts. */
    bool is_free_cluster_correct = !(diskParams.FSI_sector->FSI_Nxt_Free == 0xFFFFFFFF || diskParams.FSI_sector->FSI_Nxt_Free > (diskParams.FAT_size << 7));
    bool is_free_cluster_number_correct = !(diskParams.FSI_sector->FSI_Free_Count == 0xFFFFFFFF || (diskParams.FSI_sector->FSI_Free_Count > (diskParams.FAT_size << 7)));
    printf("Volume parameters:\n");
    printf("\tFull size: %ld MB\n", diskParams.volume_size  >> 20);
    
    if (is_free_cluster_number_correct) {
        uint64_t bytes = (diskParams.FSI_sector->FSI_Free_Count * diskParams.cluster_size_bytes) >> 20;
        printf("\tFree space: %ld MB\n", bytes);
    } else {
        printf("\tFree space: Undefined\n");
    }
    if (is_free_cluster_correct) {
        printf("\tLast occupied cluster: %d\n", diskParams.FSI_sector->FSI_Nxt_Free);
    } else {
        printf("\tLast occupied cluster: Undefined\n");
    }
    printf("\tActive cluster (cwd): %d\n", diskParams.active_dir);
}

void performCd(char* restrict path)
{
    if (path == NULL) {
        printf("Specify directory\n");
        return;
    }

    if ((path[0] == '/' || path[0] == '\\') && strlen(path) == 1) {
        diskParams.active_dir = diskParams.root_cluster;
        strcpy(CWD, "/");
        return;
    }

    char new_path[INPUT_SIZE];
    if (createCWD(path, new_path) == NULL) {
        printf("Invalid directory path\n");
        return;
    }

    char path_for_CWD[INPUT_SIZE];
    strncpy(path_for_CWD, new_path, INPUT_SIZE);

    char* token = strtok(new_path, "/");
    uint32_t active_dir = diskParams.root_cluster;
    char transformed_name[11];

    void* cluster = malloc(diskParams.cluster_size_bytes);
    if (cluster == NULL) {
        errExit("malloc");
    }
    
    while (token != NULL) {
        bool found = false;
        if (foundDIRName(token, transformed_name) == NULL) {
            printf("Unsupported symbol in the dir name \"%s\"\n", token);
            goto end;
        }

        off_t offset = diskParams.data_offset + (active_dir << diskParams.cluster_power);
        ssize_t num_read = pread(diskParams.fd, cluster, diskParams.cluster_size_bytes, offset);
        if (num_read == -1) {
            free(cluster);
            errExit("pread");
        }
        directory_t* entry = (directory_t*)cluster;
        while (entry->DIR_Name[0] != 0x00 && entry->DIR_Name[0] != 0xE5) {
            if ((entry->DIR_Attr & 0x10) && (strncmp(transformed_name, entry->DIR_Name, 11) == 0)) {
                found = true;
                break;
            }
            entry++;
            if ((void*)entry - cluster >= diskParams.cluster_size_bytes) {
                break;
            }
        }
        if (!found) {
            printf("Can't find \"%s\" directory\n", token);
            goto end;
        }

        active_dir = (entry->DIR_FstClusHI & 0xF0) | (entry->DIR_FstClusLO & 0x0F);
        // if (checkFAT) {
        // }
        token = strtok(NULL, "/\\");
    }

    diskParams.active_dir = active_dir;
    strncpy(CWD, path_for_CWD, INPUT_SIZE);
    end: ;
    free(cluster);
}

void performLs(char* restrict args)
{
    void* cluster = malloc(diskParams.cluster_size_bytes);
    if (cluster == NULL) {
        errExit("malloc");
    }
    if (readToCluster(diskParams.active_dir, cluster) == -1) {
        perror("read to cluster");
        goto end_1;
    }
    
    char convertedName[13];
    directory_t* entry = (directory_t*)cluster;
    if (diskParams.active_dir == diskParams.root_cluster)
        entry++;
    while (entry->DIR_Name[0] != 0x00 && entry->DIR_Name[0] != 0xE5) {
        FATShortToName(entry->DIR_Name, convertedName);
        printf("%s  ", convertedName);
        entry++;
        if ((void*)entry - cluster >= diskParams.cluster_size_bytes)
            break;
    }
    puts("");

    end_1: ;
    free(cluster);
}

void performMkdir(char* restrict dir_name)
{
    if (dir_name == NULL) {
        printf("Specify directory name\n");
        return;
    }

    char transformed_name[11];
    if (nametoFATShort(dir_name, transformed_name) == NULL) {
        printf("Unsupported symbol in the dir name.\n");
        return;
    }
    
    void* cluster = malloc(diskParams.cluster_size_bytes);
    if (cluster == NULL) {
        errExit("malloc");
    }
    if (readToCluster(diskParams.active_dir, cluster) == -1) {
        perror("read to cluster");
        goto end_1;
    }
    
    directory_t* entry = (directory_t*)cluster;
    while (entry->DIR_Name[0] != 0x00 && entry->DIR_Name[0] != 0xE5) {
        if ((entry->DIR_Attr & 0x10) && (strncmp(transformed_name, entry->DIR_Name, 11) == 0)) {
            printf("Such directory already exists\n");
            goto end_1;
        }
        entry++;
        if ((void*)entry - cluster >= diskParams.cluster_size_bytes) {
            printf("Not enough space in the directory\n");
            goto end_1;
        }
    }

    uint32_t free_cluster_number;
    FSInfoSector_t* fsi = findFreeCluster(&free_cluster_number);
    if (fsi == NULL) {
        perror("Not enough free space\n");
        goto end_1;
    }

    dateTime_t dateTime;
    if (genDateTime(&dateTime) == NULL) {
        perror("time generation"); 
        goto end_1;
    }

    strncpy(entry->DIR_Name, transformed_name, 11);
    entry->DIR_Attr = 0x30;
    entry->DIR_NTRes = 0;
    entry->DIR_FstClusHI = free_cluster_number & 0xF0;
    entry->DIR_FstClusLO = free_cluster_number & 0x0F;
    entry->DIR_CrtDate = dateTime.date;
    entry->DIR_CrtTime = dateTime.time;
    entry->DIR_CrtTimeTenth = dateTime.time_tenth;
    entry->DIR_LstAccDate = dateTime.date;
    entry->DIR_WrtTime = dateTime.time;
    entry->DIR_WrtDate = dateTime.date;
    entry->DIR_FileSize = 0;

    fsi->FSI_Free_Count -= 1;
    fsi->FSI_Nxt_Free = free_cluster_number;

    void* new_cluster = calloc(diskParams.cluster_size_bytes, 1);
    if (cluster == NULL) {
        perror("calloc");
        goto end_1;
    }
    directory_t* new_entry = (directory_t*)new_cluster;
    
    memcpy(new_cluster, entry, sizeof(directory_t));
    strncpy(new_entry->DIR_Name, ".          ", 11);

    new_entry += 1;
    memcpy(new_entry, cluster, sizeof(directory_t));
    strncpy(new_entry->DIR_Name, "..         ", 11);
    if (writeToCluster(free_cluster_number, new_cluster) == -1) {
        perror("write to cluster");
        goto end_2;
    }
    if (writeToFAT(free_cluster_number, 0x0FFFFFFF) == -1) {
        perror("write to fat");
        goto end_2;
    }
    if (writeToCluster(diskParams.active_dir, cluster) == -1) {
        perror("write to cluster");
        goto end_2;
    }

    // printf("Generated Date: %d/%02d/%02d\n", entry->DIR_CrtDate & 0x1F, (entry->DIR_CrtDate >> 5) & 0x0F, (entry->DIR_CrtDate >> 9) + 1980);
    // printf("Generated Time: %02d:%02d:%02d\n", entry->DIR_CrtTime >> 11, (entry->DIR_CrtTime >> 5) & 0x3F, (entry->DIR_CrtTime & 0x1F) * 2);

    end_2: ;
    free(new_cluster);
    end_1: ;
    free(cluster);
}

/* "touch" will create a file with a size of cluster size just for demonstration purposes */
void performTouch(char* restrict file_name)
{
    if (file_name == NULL) {
        printf("Specify file name\n");
        return;
    }

    char transformed_name[11];
    if (nametoFATShort(file_name, transformed_name) == NULL) {
        printf("Unsupported symbol in the file name.\n");
        return;
    }
    
    void* cluster = malloc(diskParams.cluster_size_bytes);
    if (cluster == NULL) {
        errExit("malloc");
    }
    if (readToCluster(diskParams.active_dir, cluster) == -1) {
        perror("read to cluster");
        goto end_1;
    }
    
    bool file_exists = false;
    directory_t* entry = (directory_t*)cluster;
    while (entry->DIR_Name[0] != 0x00 && entry->DIR_Name[0] != 0xE5) {
        if (!(entry->DIR_Attr & 0x10) && (strncmp(transformed_name, entry->DIR_Name, 11) == 0)) {
            file_exists = true;
            break;
        }
        entry++;
        if ((void*)entry - cluster >= diskParams.cluster_size_bytes) {
            printf("Not enough space in the directory\n");
            goto end_1;
        }
    }

    dateTime_t dateTime;
    if (genDateTime(&dateTime) == NULL) {
        perror("time generation"); 
        goto end_1;
    }

    if (file_exists) {
        entry->DIR_LstAccDate = dateTime.date;
        goto end_2;
    }

    uint32_t free_cluster_number;
    FSInfoSector_t* fsi = findFreeCluster(&free_cluster_number);
    if (fsi == NULL) {
        perror("Now enough free space\n");
        goto end_1;
    }

    strncpy(entry->DIR_Name, transformed_name, 11);
    entry->DIR_Attr = 0x20;
    entry->DIR_NTRes = 0;
    entry->DIR_FstClusHI = free_cluster_number & 0xF0;
    entry->DIR_FstClusLO = free_cluster_number & 0x0F;
    entry->DIR_CrtDate = dateTime.date;
    entry->DIR_CrtTime = dateTime.time;
    entry->DIR_CrtTimeTenth = dateTime.time_tenth;
    entry->DIR_LstAccDate = dateTime.date;
    entry->DIR_WrtTime = dateTime.time;
    entry->DIR_WrtDate = dateTime.date;
    entry->DIR_FileSize = diskParams.cluster_size_bytes;

    fsi->FSI_Free_Count -= 1;
    fsi->FSI_Nxt_Free = free_cluster_number;

    if (writeToFAT(free_cluster_number, 0x0FFFFFFF) == -1) {
        perror("write to fat");
        goto end_1;
    }
    end_2: ;
    if (writeToCluster(diskParams.active_dir, cluster) == -1) {
        perror("write to cluster");
        goto end_1;
    }
    // printf("Generated Date: %d/%02d/%02d\n", entry->DIR_CrtDate & 0x1F, (entry->DIR_CrtDate >> 5) & 0x0F, (entry->DIR_CrtDate >> 9) + 1980);
    // printf("Generated Time: %02d:%02d:%02d\n", entry->DIR_CrtTime >> 11, (entry->DIR_CrtTime >> 5) & 0x3F, (entry->DIR_CrtTime & 0x1F) * 2);
    
    end_1: ;
    free(cluster);
}

void performExit(char* restrict msg)
{
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
    if (close(diskParams.fd) == -1) {
        perror("close");
    }
    if (msg != NULL) {
        perror(msg);
        exit(1);
    }
    exit(0);
}