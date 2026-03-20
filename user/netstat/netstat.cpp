#include <stdio.h>
#include <string.h>
#include <file.h>
#include <stdlib.h>
#include <format.h>

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

// Manual right-align into fixed width
static void pad_right(char* out, const char* str, int width) {
    int len = strlen(str);
    int pad = width - len;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) out[i] = ' ';
    strcpy(out + pad, str);
}

// Manual left-align into fixed width
static void pad_left(char* out, const char* str, int width) {
    int len = strlen(str);
    strcpy(out, str);
    for (int i = len; i < width; i++) out[i] = ' ';
    out[width > len ? width : len] = '\0';
}

int main(int argc, char** argv) {
    int dir = opendir("/proc/net/tcp");
    if (dir < 0) {
        printf("netstat: cannot open /proc/net/tcp\n");
        return 1;
    }

    char c_proto[8], c_local[24], c_remote[24], c_state[16];
    pad_left(c_proto,  "Proto", 7);
    pad_left(c_local,  "Local Address", 22);
    pad_left(c_remote, "Remote Address", 22);
    pad_left(c_state,  "State", 14);
    printf("%s %s %s %s\n", c_proto, c_local, c_remote, c_state);

    dirent entry;
    while (readdir(dir, &entry) > 0) {
        char path[128];
        snprintf(path, sizeof(path), "/proc/net/tcp/%s", entry.name);

        int fd = open(path, 0);
        if (fd < 0) continue;

        char buf[1024];
        int total = 0;
        int n;
        while ((n = read(fd, buf + total, sizeof(buf) - total - 1)) > 0)
            total += n;
        close(fd);
        if (total <= 0) continue;
        buf[total] = '\0';

        char local[32], remote[32], state[20];
        if (!find_field(buf, "local", local, sizeof(local)))   strcpy(local, "?");
        if (!find_field(buf, "remote", remote, sizeof(remote))) strcpy(remote, "?");
        if (!find_field(buf, "state", state, sizeof(state)))   strcpy(state, "?");

        pad_left(c_proto,  "tcp", 7);
        pad_left(c_local,  local, 22);
        pad_left(c_remote, remote, 22);
        pad_left(c_state,  state, 14);
        printf("%s %s %s %s\n", c_proto, c_local, c_remote, c_state);
    }

    closedir(dir);
    return 0;
}