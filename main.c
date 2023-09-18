#include "commands.h"
#include "fat32impl.h"
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char CWD[INPUT_SIZE];
extern struct _diskParams_t diskParams;
/* function for abnormal program termination*/
void (*errExit)(char* );

struct Command{
    const char* name;
    void (*function)(char*);
};

int main(int argc, char** argv)
{
    if (argc != 2 || strcmp(argv[1], "--help") == 0) {
        printf("%s <path-to-disk>\n\n\tCommands: format, ls, cd, mkdir, touch, disk/volume, update, quit/exit\n", argv[0]);
        exit(1);
    }

    errExit = performExit;

    mountDisk(argv[1]);

    diskParams.FAT32 = isFAT32Disk();
    // printf("Fat32? %s\n", (diskParams.FAT32) ? "yes" : "no");
    if (diskParams.FAT32)
        setDiskParams();

    struct Command commands[] = {
        {"cd", performCd},
        {"ls", performLs},
        {"mkdir", performMkdir},
        {"format", performFormat},
        {"touch", performTouch},
        {"disk", performDisk},
        {"volume", performDisk},
        {"update", performUpdate},
        {"quit", performExit},
        {"exit", performExit}
    };
    const size_t commands_number = sizeof(commands) / sizeof(commands[0]);

    char input[INPUT_SIZE];

    for(;;) {
        memset(input, 0, INPUT_SIZE);
        printf("%s>", CWD);
        if (fgets(input, INPUT_SIZE, stdin) == NULL)
            errExit("fgets");

        size_t len = strlen(input);
        if (len == 1 && input[0] == '\n')
            continue;
        if (len > 0 && input[len - 1] == '\n')
            input[len - 1] = '\0';

        int found = 0;

        char* token = strtok(input, " ");
        if (token != NULL) {
            for (size_t i = 0; i < commands_number; i++) {
                if (strcmp(input, commands[i].name) == 0) {
                    found = 1;
                    if (!diskParams.FAT32) {
                        if (commands[i].function != performFormat && commands[i].function != performExit) {
                            printf("Unknown disk format. Use command 'format' to format the disk.\n");
                            break;
                        }
                    }
                    token = strtok(NULL, " ");
                    // printf("args %s\n", arg);
                    commands[i].function(token);
                    break;
                }
            }
        }

        if (!found)
            printf("Unknown command: %s\n", input);
    }

    return 0;
}