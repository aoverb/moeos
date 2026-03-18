#include <file.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: mkdir <path>\n");
        return 1;
    }
    if (mkdir(argv[1]) < 0) {
        printf("mkdir: failed to create directory '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}