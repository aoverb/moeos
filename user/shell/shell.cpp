#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <format.h>
#include <syscall_def.h>
#include <file.h>

#include <sys/wait.h>

constexpr char* PATH[2] = {
    "/usr/bin/",
    "/"
};

constexpr int MAX_ARGS = 64;
constexpr int MAX_INPUT = 256;
constexpr int MAX_PATH = 512;
constexpr int BUFFER_SIZE = 32768;

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
                *p = '\0'; // 截断
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

void print_rumia_text() {
    set_color(0xF0B526);
    printf("Rumi");
    set_color(0xEB392D);
    printf("a");
    set_color(0xFFFFFF);
}

void print_lolios() {
    set_color(0xEB9D2F);
    printf("LoliOS");
    set_color(0xFFFFFF);
}

bool try_exec(const char* cmd, int argc, char* argv[]) {
    char buffer[BUFFER_SIZE];
    char fn[MAX_PATH];

    for (int i = 0; i < 2; ++i) {
        snprintf(fn, sizeof(fn), "%s%s", PATH[i], cmd);

        int fd = open(fn, 1);
        if (fd == -1) {
            continue;
        }

        int size = read(fd, buffer, BUFFER_SIZE);
        if (fd == -1) {
            continue;
        }
        
        int child_pid = exec(buffer, size, 1, argc, argv);
        int ret = waitpid(child_pid);
        return true;
    }
    return false;
}

void builtin_cd(int argc, char* argv[]) {
    if (argc < 2) {
        if (chdir("/") != 0) {
            printf("cd: failed to change to /\n");
        }
    } else {
        if (chdir(argv[1]) != 0) {
            printf("cd: no such directory '%s'\n", argv[1]);
        }
    }
}

int main(int argc_main, char** argv_main) {
    char input[MAX_INPUT];
    char* tokens[MAX_ARGS];

    printf("Shell is running in user addr: %x\n", &main);

    while (1) {
        print_lolios();
        printf(">");

        getline(input, MAX_INPUT);

        // 分词
        int argc = tokenize(input, tokens, MAX_ARGS);
        if (argc == 0)
            continue;

        const char* cmd = tokens[0];

        if (strcmp(cmd, "help") == 0) {
            printf("Hello user! This is ");
            print_lolios();
            printf("!\n");
            printf("The host here is ");
            print_rumia_text();
            printf("! Feel free!\n");
            printf("Built-in commands: help, exit, cd, test\n");

        } else if (strcmp(cmd, "exit") == 0) {
            print_rumia_text();
            printf(": Goodbye, aoverb!\n");
            break;

        } else if (strcmp(cmd, "cd") == 0) {
            builtin_cd(argc, tokens);

        } else {
            if (!try_exec(cmd, argc, tokens)) {
                print_rumia_text();
                printf(": Unknown command '%s'!\n", cmd);
            }
        }

        printf("\n");
    }
    return 0;
}