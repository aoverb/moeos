#include <stdio.h>

int main(int argc, char** argv) {
    bool newline = true;
    int start = 1;

    // -n 选项：不输出末尾换行
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0') {
        newline = false;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        if (i > start)
            putchar(' ');
        printf("%s", argv[i]);
    }

    if (newline)
        putchar('\n');

    return 0;
}