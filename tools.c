#include "tools.h"
#include "fat32impl.h"

// #include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

char CWD[INPUT_SIZE] = "/";
extern struct _diskParams_t diskParams;

int writeToFAT(uint32_t N, uint32_t value)
{
    uint32_t FAT_cell_number = N;
    uint32_t sector_number = diskParams.reserved_sectors + (FAT_cell_number >> 7);
    uint32_t page_number = (sector_number << 9) / diskParams.page_size;
    uint32_t this_page_offset = FAT_cell_number % (diskParams.page_size >> 2);

    if (diskParams.FAT_sector == NULL) {
        diskParams.FAT_sector = mmap(NULL, SECTOR_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, diskParams.fd, page_number * diskParams.page_size);
        if (diskParams.FAT_sector == MAP_FAILED) {
            perror("mmap");
            return -1;
        }
        diskParams.FAT_sector_number = page_number;
    }

    if (diskParams.FAT_sector_number != page_number) {
        if (diskParams.FAT_sector == NULL) {
            perror("NULL FAT sector");
            return -1;
        }
        if (msync(diskParams.FAT_sector, SECTOR_SIZE, MS_SYNC) == -1) {
            perror("msync");
            return -1;
        }
        if (munmap(diskParams.FAT_sector, SECTOR_SIZE) == -1) {
            perror("munmap");
            return -1;
        }

        diskParams.FAT_sector = mmap(NULL, SECTOR_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, diskParams.fd, page_number << 9);
        if (diskParams.FAT_sector == MAP_FAILED) {
            perror("mmap");
            return -1;
        }

        diskParams.FAT_sector_number = page_number;
    }

    diskParams.FAT_sector[this_page_offset] = (diskParams.FAT_sector[this_page_offset] & 0XF0000000) | (value & 0X0FFFFFFF);

    return 0;
}

int writeToCluster(uint32_t N, void* cluster)
{
    off_t offset = diskParams.data_offset + (N << diskParams.cluster_power);
    ssize_t num_write = pwrite(diskParams.fd, cluster, diskParams.cluster_size_bytes, offset);
    if (num_write == -1) {
        perror("pwrite to cluster");
        return -1;
    }
    return 0;
}

int readToCluster(uint32_t N, void* cluster)
{
    off_t offset = diskParams.data_offset + (N << diskParams.cluster_power);
    ssize_t num_read = pread(diskParams.fd, cluster, diskParams.cluster_size_bytes, offset);
    if (num_read == -1) {
        perror("cluster pread");
        return -1;
    }
    return 0;
}

char* foundDIRName(char* restrict src, char* restrict dest)
{
    char convertedName[11];
    memset(convertedName, ' ', 11);
    int index = 0;
    bool dot = false;

    if (*src == '.') {
        convertedName[0] = '.';
        ++src;
        if (*src == '.') {
            convertedName[1] = '.';
        } else if (*src != '\0') {
            return NULL;
        }
        memcpy(dest, convertedName, 11);
        return dest;
    }

    while (*src) {
        if ((*src) == '.') {
            dot = true;
            index = 8;
            src++;
            continue;
        }

        if (isalpha(*src) || isdigit(*src)) {
            if ((index < 8 && !dot) || (index < 11 && dot)) {
                convertedName[index] = toupper(*src);
                index++;
            }
            src++;
        } else {
            return NULL;
        }
    }
    memcpy(dest, convertedName, 11);
    return dest;
}

char* nametoFATShort(char* restrict src, char* restrict dest)
{
    char convertedName[11];
    memset(convertedName, ' ', 11);
    int index = 0;
    bool dot = false;
    while (*src) {
        if ((*src) == '.') {
            dot = true;
            index = 8;
            src++;
            continue;
        }

        if (isalpha(*src) || isdigit(*src)) {
            if ((index < 8 && !dot) || (index < 11 && dot)) {
                convertedName[index] = toupper(*src);
                index++;
            }
            src++;
        } else {
            return NULL;
        }
    }
    memcpy(dest, convertedName, 11);
    return dest;
}

void FATShortToName(char* restrict src, char* restrict dest)
{
    bool dot = false;
    bool first_space = false;
    int j = 0;
    for (int i = 0; i < 11; ++i) {
        if (src[i] == ' ') {
            first_space = true;
            continue;
        }
        if (first_space && !dot) {
            dest[j] = '.';
            dot = true;
            j++;
        }
        dest[j] = tolower(src[i]);
        j++;
    }
    dest[j] = '\0';
}

dateTime_t* genDateTime(dateTime_t* restrict dest)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        return NULL;
    }

    time_t current_time;
    struct tm* timeinfo;
    time(&current_time);
    timeinfo = localtime(&current_time);

    dest->date = (timeinfo->tm_year - 80) << 9 | (timeinfo->tm_mon + 1) << 5 | timeinfo->tm_mday;
    dest->time = timeinfo->tm_hour << 11 | timeinfo->tm_min << 5 | (timeinfo->tm_sec / 2);
    dest->time_tenth = timeinfo->tm_sec / 30 * 100 + ts.tv_nsec / 10000000;
    return dest;
}

char* createCWD(char* restrict path, char* restrict dest)
{
    char buff[INPUT_SIZE];
    char* p_buff = buff;
    if (path[0] != '/' && path[0] != '\\') {
        strncpy(buff, CWD, INPUT_SIZE);
        p_buff += strlen(CWD);
    }
    char* token = strtok(path, "/\\");
    while (token != NULL) {
        if (strcmp(".", token) == 0) {
            token = strtok(NULL, "/\\");
            continue;
        }
        if (strcmp("..", token) == 0) {
            char* str = strrchr(buff, '/');
            if (str != buff) {
                p_buff = str;
                *p_buff = '\0';
            } else {
                buff[1] = '\0';
                p_buff = &buff[1];
            }
            token = strtok(NULL, "/\\");
            continue;
        }
        size_t token_len = strlen(token);
        if (strlen(p_buff) + token_len + 1 > INPUT_SIZE) {
            return NULL;
        }
        if (*(p_buff-1) != '/') {
            *p_buff = '/';
            p_buff++;
        }
        strcpy(p_buff, token);
        p_buff += token_len;
        token = strtok(NULL, "/\\");
    }
    strncpy(dest, buff, INPUT_SIZE);
    return dest;
}