#ifndef COMMANDS_H
#define COMMANDS_H

void performFormat(char* restrict args);
void performUpdate(char* restrict args);
void performDisk(char* restrict args);
void performCd(char* restrict path);
void performLs(char* restrict args);
void performMkdir(char* restrict dir_name);
void performTouch(char* restrict file_name);
void performExit(char* restrict msg);

#endif