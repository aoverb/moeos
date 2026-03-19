// kill.cpp — send kill signal to a process
#include <stdio.h>
#include <signal.h>
#include <string.h>

extern int kill(pid_t pid);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: kill <pid>\n");
        return 1;
    }

    int pid = atoi(argv[1]);
    if (pid <= 0) {
        printf("kill: invalid pid '%s'\n", argv[1]);
        return 1;
    }

    int ret = kill((pid_t)pid);
    if (ret < 0) {
        printf("kill: failed to kill pid %d\n", pid);
        return 1;
    }

    return 0;
}