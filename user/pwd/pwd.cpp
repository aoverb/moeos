#include <stdio.h>
#include <file.h>

int main(int argc, char** argv) {
    char path[255];
    if (getcwd(path, 255) == 0) {
        printf("%s\n", path);
        return 0;
    }
    return -1;
}