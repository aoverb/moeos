#include <file.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: rm <path>\n");
        return 1;
    }
    if (unlink(argv[1]) < 0) {
        printf("rm: failed to remove '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}