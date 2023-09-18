#ifndef FAT32IMPL_H
#define FAT32IMPL_H

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>

#define SECTOR_SIZE 512
#define DEFAULT_SECT_PER_CLUS 2
#define DEFAULT_DISK_SIZE (20*1024*1024) // 20 MB
#define DEFAULT_BACKUP_SECTOR 6
#define CLUSTER_SIZE (SECTOR_SIZE * DEFAULT_SECT_PER_CLUS)
// #define EOC 0x0FFFFFFF

// const char default_volume_label[11] = "NO NAME    ";
#define DEFAULT_VOLUME_LABEL "NO NAME    "

typedef union _bootSector {
    struct {
        uint8_t     BS_jumpBoot[3],
                    BS_OEMName[8];
        uint16_t    BPB_BytesPerSec;
        uint8_t     BPB_SecPerClus;
        uint16_t    BPB_RsvdSecCnt;
        uint8_t     BPB_NumFATs;
        uint16_t    BPB_RootEntCnt,
                    BPB_TotSec16;
        uint8_t     BPB_Media;
        uint16_t    BPB_FATSz16,
                    BPB_SecPerTrk,
                    BPB_NumHeads;
        uint32_t    BPB_HiddSec,
                    BPB_TotSec32,
                    BPB_FATSz32;
        uint16_t    BPB_ExtFlags;
        uint8_t     BPB_FSVer[2];
        uint32_t    BPB_RootClus;
        uint16_t    BPB_FSInfo,
                    BPB_BkBootSec;
        uint8_t     BPB_Reserved[12],
                    BS_DrvNum,
                    BS_Reserved1,
                    BS_BootSig;
        uint32_t    BS_VolID;
        uint8_t     BS_VolLab[11],
                    BS_FilSysType[8];
    } __attribute__((packed));
    uint8_t offset[SECTOR_SIZE];
} bootSector_t;

typedef union _FSInfoSector {
    struct {
        uint32_t    FSI_LeadSig;
        uint8_t     FSI_Reserved1[480];
        uint32_t    FSI_StrucSig,
                    FSI_Free_Count,
                    FSI_Nxt_Free;
        uint8_t     FSI_Reserved2[12];
        uint32_t    FSI_TrailSig;
    } __attribute__((packed));
    uint8_t offset[SECTOR_SIZE];
} FSInfoSector_t;

typedef union _directory {
    struct {
        uint8_t     DIR_Name[11],
                    DIR_Attr,
                    DIR_NTRes,
                    DIR_CrtTimeTenth;
        uint16_t    DIR_CrtTime,
                    DIR_CrtDate,
                    DIR_LstAccDate,
                    DIR_FstClusHI,
                    DIR_WrtTime,
                    DIR_WrtDate,
                    DIR_FstClusLO;
        uint32_t    DIR_FileSize;
    } __attribute__((packed));
    uint8_t offset[32];
} directory_t;

struct _diskParams_t {
    FSInfoSector_t* FSI_sector;
    uint32_t* FAT_sector;
    uint32_t FAT_sector_number;
    int fd;
    off_t volume_size;
    uint32_t FAT_size;
    uint32_t root_cluster;
    off_t data_offset;
    size_t cluster_size_bytes;
    uint32_t active_dir;
    uint32_t page_size;
    uint16_t reserved_sectors;
    char volume_label[11];
    uint8_t cluster_power; // for faster calculations
    bool FAT32;
};

void initBootSector();
void initFSInfoSector();
void initFATTables();
void initRoot();

bool isFAT32Disk();
void setDiskParams();
void mountDisk(char* volume);

int cashFSISector();
FSInfoSector_t* findFreeCluster(uint32_t* restrict dest);

#endif /* FAT32IMPL_H*/