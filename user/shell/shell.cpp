#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <format.h>
#include <file.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" int pipe(int fds[2]);

#define O_RDONLY  0x01
#define O_WRONLY  0x02
#define O_RDWR    0x03
#define O_CREATE  0x04

constexpr uint8_t PATH_LIST_SIZE = 2;
constexpr const char* PATH[PATH_LIST_SIZE] = {
    "/usr/bin/",
    "/"
};

constexpr int MAX_ARGS     = 64;
constexpr int MAX_INPUT    = 256;
constexpr int MAX_PATH     = 512;
constexpr int MAX_PIPELINE = 16;

static char cwd[255];

// ─── 分词 ───

int tokenize(char* input, char* tokens[], int max_tokens) {
    int count = 0;
    char* p = input;

    while (*p && count < max_tokens) {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0')
            break;

        if (*p == '"') {
            ++p;
            tokens[count++] = p;
            while (*p && *p != '"')
                ++p;
            if (*p == '"') {
                *p = '\0';
                ++p;
            }
        } else {
            tokens[count++] = p;
            while (*p && *p != ' ' && *p != '\t')
                ++p;
            if (*p) {
                *p = '\0';
                ++p;
            }
        }
    }
    return count;
}

// ─── 管道行解析 ───

struct stage {
    char* argv[MAX_ARGS];
    int   argc;
};

int split_pipeline(char* tokens[], int token_count, stage stages[], int max_stages) {
    int n = 0;
    stages[0].argc = 0;

    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (stages[n].argc == 0) return -1;
            stages[n].argv[stages[n].argc] = nullptr;
            ++n;
            if (n >= max_stages) return -1;
            stages[n].argc = 0;
        } else {
            stages[n].argv[stages[n].argc++] = tokens[i];
        }
    }

    if (stages[n].argc == 0) return -1;
    stages[n].argv[stages[n].argc] = nullptr;
    return n + 1;
}

// ─── 重定向解析 ───
// 从 tokens 中找到 ">"，提取文件名，并将其从 token 列表中移除
// 返回重定向文件名，nullptr 表示无重定向，(char*)-1 表示语法错误

char* extract_redirect(char* tokens[], int& token_count) {
    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 >= token_count) return (char*)-1;
            char* filename = tokens[i + 1];
            // 把后面的 token 往前挪，覆盖 > 和 filename
            for (int j = i; j + 2 < token_count; j++)
                tokens[j] = tokens[j + 2];
            token_count -= 2;
            return filename;
        }
    }
    return nullptr;
}

// ─── 路径搜索 + 加载文件到内存 ───
// 成功返回 buffer 和 size，调用者负责 free

bool load_cmd(const char* cmd, char*& buf_out, int& size_out) {
    char fn[MAX_PATH];
    file_stat fst;

    auto try_path = [&](const char* path) -> bool {
        if (stat(path, &fst) == -1) return false;
        int fd = open(path, O_RDONLY);
        if (fd == -1) return false;
        char* buffer = (char*)malloc(fst.size);
        if (!buffer) { close(fd); return false; }
        int size = read(fd, buffer, fst.size);
        close(fd);
        if (size <= 0) { free(buffer); return false; }
        buf_out = buffer;
        size_out = size;
        return true;
    };

    // 绝对路径 / 相对路径
    if (cmd[0] == '/' || cmd[0] == '.') {
        return try_path(cmd);
    }

    // 搜索 PATH
    for (int i = 0; i < PATH_LIST_SIZE; ++i) {
        snprintf(fn, sizeof(fn), "%s%s", PATH[i], cmd);
        if (try_path(fn)) return true;
    }

    // 搜索 cwd
    snprintf(fn, sizeof(fn), "%s/%s", cwd, cmd);
    return try_path(fn);
}

// ─── 管道执行核心 ───

bool exec_pipeline(stage stages[], int stage_count, int redir_fd = -1) {
    int pipes[MAX_PIPELINE][2];
    int pids[MAX_PIPELINE];

    // 1. 创建所有管道
    for (int i = 0; i < stage_count - 1; i++) {
        if (pipe(pipes[i]) != 0) {
            printf("pipe: failed to create pipe\n");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return false;
        }
    }

    // 2. 逐个加载并 exec
    for (int i = 0; i < stage_count; i++) {
        char* buffer = nullptr;
        int size = 0;

        if (!load_cmd(stages[i].argv[0], buffer, size)) {
            printf("rumia: command not found '%s'\n", stages[i].argv[0]);
            for (int j = 0; j < stage_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return false;
        }

        fd_remap remaps[2];
        int remap_count = 0;

        if (i > 0) {
            remaps[remap_count].child_fd  = 0;           // stdin
            remaps[remap_count].parent_fd = pipes[i-1][0];
            remap_count++;
        }

        if (i < stage_count - 1) {
            remaps[remap_count].child_fd  = 1;           // stdout
            remaps[remap_count].parent_fd = pipes[i][1];
            remap_count++;
        } else if (redir_fd >= 0) {
            // 最后一个 stage，重定向 stdout 到文件
            remaps[remap_count].child_fd  = 1;
            remaps[remap_count].parent_fd = redir_fd;
            remap_count++;
        }

        pids[i] = exec(buffer, size,
                        stages[i].argc, stages[i].argv,
                        remaps, remap_count);
        free(buffer);

        if (pids[i] <= 0) {
            printf("rumia: failed to exec '%s'\n", stages[i].argv[0]);
        }
    }

    // 3. 父进程关掉所有管道 fd
    for (int i = 0; i < stage_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // 4. 等待所有子进程
    for (int i = 0; i < stage_count; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i]);
        }
    }

    return true;
}

bool exec_bidir(stage& left, stage& right) {
    int pipe_a2b[2];  // left stdout → right stdin
    int pipe_b2a[2];  // right stdout → left stdin

    if (pipe(pipe_a2b) != 0) {
        printf("pipe: failed to create pipe\n");
        return false;
    }
    if (pipe(pipe_b2a) != 0) {
        printf("pipe: failed to create pipe\n");
        close(pipe_a2b[0]);
        close(pipe_a2b[1]);
        return false;
    }

    // 加载 left
    char* buf_l = nullptr; int sz_l = 0;
    if (!load_cmd(left.argv[0], buf_l, sz_l)) {
        printf("rumia: command not found '%s'\n", left.argv[0]);
        close(pipe_a2b[0]); close(pipe_a2b[1]);
        close(pipe_b2a[0]); close(pipe_b2a[1]);
        return false;
    }

    // 加载 right
    char* buf_r = nullptr; int sz_r = 0;
    if (!load_cmd(right.argv[0], buf_r, sz_r)) {
        printf("rumia: command not found '%s'\n", right.argv[0]);
        free(buf_l);
        close(pipe_a2b[0]); close(pipe_a2b[1]);
        close(pipe_b2a[0]); close(pipe_b2a[1]);
        return false;
    }

    // left: stdin=pipe_b2a[0], stdout=pipe_a2b[1]
    fd_remap remaps_l[2] = {
        { 0, pipe_b2a[0] },   // stdin  ← right 的输出
        { 1, pipe_a2b[1] },   // stdout → right 的输入
    };
    int pid_l = exec(buf_l, sz_l, left.argc, left.argv, remaps_l, 2);
    free(buf_l);

    // right: stdin=pipe_a2b[0], stdout=pipe_b2a[1]
    fd_remap remaps_r[2] = {
        { 0, pipe_a2b[0] },   // stdin  ← left 的输出
        { 1, pipe_b2a[1] },   // stdout → left 的输入
    };
    int pid_r = exec(buf_r, sz_r, right.argc, right.argv, remaps_r, 2);
    free(buf_r);

    // 父进程关掉所有 fd
    close(pipe_a2b[0]); close(pipe_a2b[1]);
    close(pipe_b2a[0]); close(pipe_b2a[1]);

    if (pid_l <= 0) printf("rumia: failed to exec '%s'\n", left.argv[0]);
    if (pid_r <= 0) printf("rumia: failed to exec '%s'\n", right.argv[0]);

    // 等任意一个退出就回来（另一个会因管道断裂自行退出）
    if (pid_l > 0) waitpid(pid_l);
    if (pid_r > 0) waitpid(pid_r);

    return true;
}

// ─── 单命令执行 (无管道) ───

bool try_exec(const char* cmd, int argc, char* argv[], int redir_fd = -1) {
    char* buffer = nullptr;
    int size = 0;

    if (!load_cmd(cmd, buffer, size))
        return false;

    fd_remap remaps[1];
    int remap_count = 0;

    if (redir_fd >= 0) {
        remaps[0].child_fd  = 1;        // stdout
        remaps[0].parent_fd = redir_fd;
        remap_count = 1;
    }

    int child_pid = exec(buffer, size, argc, argv, remaps, remap_count);
    free(buffer);

    if (child_pid <= 0) return false;
    tcsetpgrp(0, child_pid);
    waitpid(child_pid);
    tcsetpgrp(0, getpid());
    return true;
}

// ─── 内建命令 ───

void builtin_cd(int argc, char* argv[]) {
    if (argc < 2) {
        if (chdir("/") != 0)
            printf("cd: failed to change to /\n");
    } else {
        if (chdir(argv[1]) != 0)
            printf("cd: no such directory '%s'\n", argv[1]);
    }
}

// ─── 提示符 ───

void print_rumia_text() {
    set_color(0xE8BF5A);
    printf("Rumi");
    set_color(0xD4524E);
    printf("a");
    set_color(0xF4F0EB);
}

void print_lolios() {
    set_color(0xE8BF5A);
    printf("LoliOS");
    set_color(0xF4F0EB);
}

void print_cwd() {
    if (getcwd(cwd, 255) != 0)
        return;
    set_color(0xD4524E);
    printf("%s", cwd);
    set_color(0xF4F0EB);
}

// ─── 主循环 ───

int main(int argc_main, char** argv_main) {
    char input[MAX_INPUT];
    char* tokens[MAX_ARGS];
    stage stages[MAX_PIPELINE];

    while (1) {
        set_color(0x39C5BB);
        printf("root@");
        set_color(0xF4F0EB);
        print_lolios();
        printf(":");
        print_cwd();
        printf("$ ");

        if (!getline(input, MAX_INPUT))
            break;

        int token_count = tokenize(input, tokens, MAX_ARGS);
        if (token_count == 0)
            continue;

        const char* cmd = tokens[0];

        if (strcmp(cmd, "help") == 0) {
            printf("Hello user! This is ");
            print_lolios();
            printf("!\n");
            printf("The host here is ");
            print_rumia_text();
            printf("! Feel free!\n");
            printf("Built-in: help, exit, cd\n");
            printf("Pipe: cmd1 | cmd2 | cmd3\n");
            printf("Redirect: cmd > file\n");

        } else if (strcmp(cmd, "exit") == 0) {
            print_rumia_text();
            printf(": Goodbye, aoverb!\n");
            break;

        } else if (strcmp(cmd, "cd") == 0) {
            builtin_cd(token_count, tokens);

        } else {
            // ── 解析输出重定向 ──
            char* redir_file = extract_redirect(tokens, token_count);
            int redir_fd = -1;

            if (redir_file == (char*)-1) {
                print_rumia_text();
                printf(": syntax error near '>'\n");
                printf("\n");
                continue;
            }

            if (redir_file) {
                char redir_path[MAX_PATH];
                if (redir_file[0] == '/') {
                    // 绝对路径，直接使用
                    snprintf(redir_path, sizeof(redir_path), "%s", redir_file);
                } else {
                    // 相对路径，拼上 cwd
                    snprintf(redir_path, sizeof(redir_path), "%s/%s", cwd, redir_file);
                }
                redir_fd = open(redir_path, O_WRONLY | O_CREATE);
                if (redir_fd < 0) {
                    printf("rumia: cannot open '%s' for writing\n", redir_file);
                    printf("\n");
                    continue;
                }
            }

            if (token_count == 0) {
                if (redir_fd >= 0) close(redir_fd);
                printf("\n");
                continue;
            }
            cmd = tokens[0];

            // 先检查是否是双向管道
            int bidir_pos = -1;
            for (int i = 0; i < token_count; i++) {
                if (strcmp(tokens[i], "<>") == 0) {
                    bidir_pos = i;
                    break;
                }
            }

            if (bidir_pos >= 0) {
                // 解析 left <> right
                stage left_stage, right_stage;
                left_stage.argc = 0;
                right_stage.argc = 0;

                for (int i = 0; i < bidir_pos; i++)
                    left_stage.argv[left_stage.argc++] = tokens[i];
                for (int i = bidir_pos + 1; i < token_count; i++)
                    right_stage.argv[right_stage.argc++] = tokens[i];

                left_stage.argv[left_stage.argc] = nullptr;
                right_stage.argv[right_stage.argc] = nullptr;

                if (left_stage.argc == 0 || right_stage.argc == 0) {
                    print_rumia_text();
                    printf(": syntax error near '<>'\n");
                } else {
                    exec_bidir(left_stage, right_stage);
                }
            } else {
                // 原有的单向管道逻辑
                int n = split_pipeline(tokens, token_count, stages, MAX_PIPELINE);

                if (n < 0) {
                    print_rumia_text();
                    printf(": syntax error near '|'\n");
                } else if (n == 1) {
                    if (!try_exec(cmd, stages[0].argc, stages[0].argv, redir_fd)) {
                        print_rumia_text();
                        printf(": Unknown command '%s'!\n", cmd);
                    }
                } else {
                    exec_pipeline(stages, n, redir_fd);
                }
            }

            if (redir_fd >= 0) close(redir_fd);
        }

        printf("\n");
    }
    return 0;
}