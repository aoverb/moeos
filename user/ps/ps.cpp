// ps.cpp — list processes via /proc, Linux-style output
#include <stdio.h>
#include <string.h>
#include <file.h>
#include <stdlib.h>
#include <format.h>

// Parse "key: value\n" from status file content
// Returns pointer to value string (null-terminated), or nullptr if key not found.
static bool find_field(const char* buf, const char* key, char* out, int out_size) {
    const char* line = buf;
    int keylen = strlen(key);
    while (line && *line) {
        const char* nl = strchr(line, '\n');
        if (strncmp(line, key, keylen) == 0 && line[keylen] == ':') {
            const char* val = line + keylen + 1;
            while (*val == ' ') val++;
            int len = nl ? (int)(nl - val) : (int)strlen(val);
            if (len >= out_size) len = out_size - 1;
            strncpy(out, val, len);
            out[len] = '\0';
            return true;
        }
        line = nl ? nl + 1 : nullptr;
    }
    return false;
}

// 右对齐：把 str 填充到 width 宽，左边补空格
static void pad_right(char* out, const char* str, int width) {
    int len = strlen(str);
    int pad = width - len;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) out[i] = ' ';
    strcpy(out + pad, str);
}

// 左对齐：把 str 填充到 width 宽，右边补空格
static void pad_left(char* out, const char* str, int width) {
    int len = strlen(str);
    strcpy(out, str);
    for (int i = len; i < width; i++) out[i] = ' ';
    out[width > len ? width : len] = '\0';
}

int main(int argc, char** argv) {
    int dir = opendir("/proc");
    if (dir < 0) {
        printf("ps: cannot open /proc\n");
        return 1;
    }

    printf("  PID  PPID  PRI  S  TIME     FD  CWD          CMD\n");

    dirent entry;
    while (readdir(dir, &entry) > 0) {
        // Only process numeric entries (PID directories)
        if (entry.name[0] < '0' || entry.name[0] > '9') continue;
        
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/status", entry.name);

        int fd = open(path, 0);
        if (fd < 0) continue;
        char buf[8192];
        int total = 0;
        int n;
        while ((n = read(fd, buf + total, sizeof(buf) - total - 1)) > 0){
            total += n;
        }
            
        close(fd);
        if (total <= 0) continue;
        buf[total] = '\0';
        char name[64], state[16], cwd[256];
        char pid_s[16], ppid_s[16], fd_s[16], time_s[16], priority_s[16];

        find_field(buf, "name",              name,       sizeof(name));
        find_field(buf, "pid",               pid_s,      sizeof(pid_s));
        find_field(buf, "parent pid",        ppid_s,     sizeof(ppid_s));
        find_field(buf, "file opened",       fd_s,       sizeof(fd_s));
        find_field(buf, "current directory", cwd,        sizeof(cwd));
        find_field(buf, "running time",      time_s,     sizeof(time_s));
        find_field(buf, "priority",          priority_s, sizeof(priority_s));
        find_field(buf, "state",             state,      sizeof(state));

        int pid      = pid_s      ? atoi(pid_s)      : 0;
        int ppid     = ppid_s     ? atoi(ppid_s)      : 0;
        int priority = priority_s ? atoi(priority_s)  : 0;
        int fds      = fd_s       ? atoi(fd_s)        : 0;
        int time_sec = time_s     ? atoi(time_s)      : 0;

        // Format time as MM:SS
        char timebuf[16];
        snprintf(timebuf, sizeof(timebuf), "%d:%d", time_sec / 60, time_sec % 60);

        // State: first character only (R/S/Z/W...)
        char st = (state && *state) ? state[0] : '?';

        char c_pid[8], c_ppid[8], c_pri[8], c_fd[8], c_cwd[14], c_time[10];
        char tmp[16];

        snprintf(tmp, sizeof(tmp), "%d", pid);     pad_right(c_pid,  tmp, 5);
        snprintf(tmp, sizeof(tmp), "%d", ppid);    pad_right(c_ppid, tmp, 5);
        snprintf(tmp, sizeof(tmp), "%d", priority);pad_right(c_pri,  tmp, 4);
        snprintf(tmp, sizeof(tmp), "%d", fds);     pad_right(c_fd,   tmp, 3);
        pad_left(c_time, timebuf, 8);
        pad_left(c_cwd,  cwd ? cwd : "?", 13);

        printf("%s %s %s  %c  %s %s %s%s\n",
            c_pid, c_ppid, c_pri, st, c_time, c_fd, c_cwd, name ? name : "?");
    }

    closedir(dir);
    return 0;
}