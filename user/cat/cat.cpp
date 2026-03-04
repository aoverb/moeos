#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <file.h>

// ─── 从 fd 读取并输出到 stdout ───

void cat_fd(int fd) {
    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++)
            putchar(buf[i]);
    }
}

// ─── 读取整个文件并输出 ───

bool cat_file(const char* path) {
    file_stat fst;
    if (stat(path, &fst) == -1) {
        printf("cat: cannot stat '%s'\n", path);
        return false;
    }

    int fd = open(path, 1);
    if (fd == -1) {
        printf("cat: cannot open '%s'\n", path);
        return false;
    }

    char* buffer = (char*)malloc(fst.size);
    if (!buffer) {
        close(fd);
        printf("cat: out of memory\n");
        return false;
    }

    int size = read(fd, buffer, fst.size);
    close(fd);

    for (int i = 0; i < size; i++)
        putchar(buffer[i]);

    free(buffer);
    return true;
}

// ─── main ───

int main(int argc, char** argv) {
    // 无参数 → 等同于 cat -
    if (argc <= 1) {
        cat_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            // "-" → 从 stdin(fd 0) 读取
            cat_fd(0);
        } else {
            cat_file(argv[i]);
        }
    }

    return 0;
}