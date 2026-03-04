#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <file.h>

// ─── 路径解析：将相对路径拼接到 cwd 上 ───

static char resolved[1024];

const char* resolve_path(const char* path) {
    // 绝对路径直接返回
    if (path[0] == '/')
        return path;

    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) == -1) {
        printf("cat: cannot get current directory\n");
        return path; // fallback
    }

    int cwd_len = strlen(cwd);
    int path_len = strlen(path);

    // 确保 cwd 以 '/' 结尾
    bool need_slash = (cwd_len > 0 && cwd[cwd_len - 1] != '/');

    if (cwd_len + need_slash + path_len >= (int)sizeof(resolved)) {
        printf("cat: path too long\n");
        return path;
    }

    memcpy(resolved, cwd, cwd_len);
    if (need_slash)
        resolved[cwd_len++] = '/';
    memcpy(resolved + cwd_len, path, path_len + 1);
    return resolved;
}

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
    const char* full = resolve_path(path);

    file_stat fst;
    if (stat(full, &fst) == -1) {
        printf("cat: cannot stat '%s'\n", path);
        return false;
    }

    int fd = open(full, 1);
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
    if (argc <= 1) {
        cat_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0)
            cat_fd(0);
        else
            cat_file(argv[i]);
    }

    return 0;
}