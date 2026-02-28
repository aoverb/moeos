#include <stdio.h>
#include <string.h>
#include <file.h>

int main(int argc, char** argv) {
    char path[255];
    if (getcwd(path, 255) != 0) {
        return -1;
    }
    int fd = opendir(path);
    bool showall = false;
    if (argc > 1) {
        if (strcmp(argv[1], "-a") == 0) {
            showall = true;
        }
    }
    if (fd == -1) {
        return -1;
    }
    dirent my_dirent;
    while(readdir(fd, &my_dirent) == 1) {
        if (!showall) {
            if (strcmp(my_dirent.name, ".") == 0 ||
            strcmp(my_dirent.name, "..") == 0) {
                continue;
            }
        } 
        printf("%-4d %-12s %c\n", my_dirent.inode, my_dirent.name, my_dirent.type);
    }
    closedir(fd);
    return 0;
}