#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <file.h>

// ─── 子串匹配 ───

bool strstr_match(const char* haystack, const char* needle, int needle_len) {
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (j < needle_len && haystack[i + j] && haystack[i + j] == needle[j])
            j++;
        if (j == needle_len)
            return true;
    }
    return false;
}

// ─── 从 buffer 中逐行匹配并输出 ───

void grep_buffer(const char* buf, int size, const char* pattern,
                 int pattern_len, const char* filename, bool show_filename,
                 bool show_linenum, bool invert) {
    int line_start = 0;
    int linenum = 1;

    for (int i = 0; i <= size; i++) {
        if (i == size || buf[i] == '\n') {
            // 提取当前行（不含换行符）
            int line_len = i - line_start;
            // 临时 null-terminate：拷贝到栈上
            char line[1024];
            int copy_len = line_len < 1023 ? line_len : 1023;
            for (int k = 0; k < copy_len; k++)
                line[k] = buf[line_start + k];
            line[copy_len] = '\0';

            bool matched = strstr_match(line, pattern, pattern_len);
            if (matched != invert) {
                if (show_filename)
                    printf("%s:", filename);
                if (show_linenum)
                    printf("%d:", linenum);
                printf("%s\n", line);
            }

            line_start = i + 1;
            linenum++;
        }
    }
}

// ─── 从 fd 逐块读取并按行匹配（用于 stdin 管道）───

void grep_fd(int fd, const char* pattern, int pattern_len,
             const char* prefix, bool show_linenum, bool invert) {
    char buf[512];
    char line[1024];
    int line_pos = 0;
    int linenum = 1;
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n' || line_pos >= 1023) {
                line[line_pos] = '\0';

                bool matched = strstr_match(line, pattern, pattern_len);
                if (matched != invert) {
                    if (prefix)
                        printf("%s:", prefix);
                    if (show_linenum)
                        printf("%d:", linenum);
                    printf("%s\n", line);
                }

                line_pos = 0;
                linenum++;
            } else {
                line[line_pos++] = buf[i];
            }
        }
    }

    // 处理末尾没有换行的最后一行
    if (line_pos > 0) {
        line[line_pos] = '\0';
        bool matched = strstr_match(line, pattern, pattern_len);
        if (matched != invert) {
            if (prefix)
                printf("%s:", prefix);
            if (show_linenum)
                printf("%d:", linenum);
            printf("%s\n", line);
        }
    }
}

// ─── 读取整个文件 ───

bool grep_file(const char* path, const char* pattern, int pattern_len,
               bool show_filename, bool show_linenum, bool invert) {
    file_stat fst;
    if (stat(path, &fst) == -1) {
        printf("grep: cannot stat '%s'\n", path);
        return false;
    }

    int fd = open(path, 1);
    if (fd == -1) {
        printf("grep: cannot open '%s'\n", path);
        return false;
    }

    char* buffer = (char*)malloc(fst.size);
    if (!buffer) {
        close(fd);
        printf("grep: out of memory\n");
        return false;
    }

    int size = read(fd, buffer, fst.size);
    close(fd);

    if (size <= 0) {
        free(buffer);
        return false;
    }

    grep_buffer(buffer, size, pattern, pattern_len,
                path, show_filename, show_linenum, invert);
    free(buffer);
    return true;
}

// ─── main ───

int main(int argc, char** argv) {
    bool show_linenum = false;
    bool invert = false;

    // 解析选项
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        char* flag = argv[argi] + 1;
        while (*flag) {
            if (*flag == 'n')      show_linenum = true;
            else if (*flag == 'v') invert = true;
            else {
                printf("grep: unknown option '-%c'\n", *flag);
                return 1;
            }
            flag++;
        }
        argi++;
    }

    // pattern 是第一个非选项参数
    if (argi >= argc) {
        printf("Usage: grep [-nv] PATTERN [FILE...]\n");
        printf("  -n  show line numbers\n");
        printf("  -v  invert match\n");
        return 1;
    }

    const char* pattern = argv[argi++];
    int pattern_len = strlen(pattern);

    int file_count = argc - argi;

    // 没有文件参数 → 从 stdin(fd 0) 读取，支持管道
    if (file_count == 0) {
        grep_fd(0, pattern, pattern_len, nullptr, show_linenum, invert);
        return 0;
    }

    bool show_filename = (file_count > 1);

    for (int i = argi; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            // "-" → 从 stdin 读取
            const char* prefix = show_filename ? "(stdin)" : nullptr;
            grep_fd(0, pattern, pattern_len, prefix, show_linenum, invert);
        } else {
            grep_file(argv[i], pattern, pattern_len,
                      show_filename, show_linenum, invert);
        }
    }

    return 0;
}