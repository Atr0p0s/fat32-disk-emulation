# fat32-disk-emulation
Simple FAT32 file system emulator.

Program takes a file path as a parameter, if it doesn't exist - it create a new "disk" with a size 20 of 20 MB.
Program prints path in console relatively internal file system.

Supported commands:
- *format* - formats file to FAT32;
- *ls* - shows files and catalogs in the working directory;
- *cd <path>* - change the current working directory;
- *mkdir <name>* - creates catalog in the working directory;
- *touch <name>* - creates/updates file in the working directory;
- *disk* / *volume* - shows volume info;
- *update* - update volume info (in case of disk formatting). It updates automatically after creating a file/catalog.
- *quit* / *exit* - saves changes on the disk (they may be cached) and close the program.

### Build
*make build*

*./f32disk <file_path>* - run the program
