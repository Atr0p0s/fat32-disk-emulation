#ifndef TOOLS_H
#define TOOLS_H

#include <stdint.h>

#define INPUT_SIZE 128

typedef struct _dateTime {
    uint16_t date;
    uint16_t time;
    uint8_t time_tenth;
} dateTime_t;

int writeToFAT(uint32_t cluster_number, uint32_t value);
int writeToCluster(uint32_t cluster_number, void* cluster);
int readToCluster(uint32_t cluster_number, void* cluster);

char* foundDIRName(char* restrict src, char* restrict dest);
char* nametoFATShort(char* restrict src, char* restrict dest);
void FATShortToName(char* restrict src, char* restrict dest);

dateTime_t* genDateTime(dateTime_t* restrict dest);
char* createCWD(char* restrict path, char* restrict dest);

#endif