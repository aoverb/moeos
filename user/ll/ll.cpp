#include <stdio.h>
#include <string.h>
#include <file.h>

static void mode_to_str(uint32_t mode, uint8_t type, char* out) {
    out[0] = (type == 0) ? 'd' : '-';
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 040)  ? 'r' : '-';
    out[5] = (mode & 020)  ? 'w' : '-';
    out[6] = (mode & 010)  ? 'x' : '-';
    out[7] = (mode & 04)   ? 'r' : '-';
    out[8] = (mode & 02)   ? 'w' : '-';
    out[9] = (mode & 01)   ? 'x' : '-';
    out[10] = '\0';
}

static const char* month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void timestamp_to_str(uint32_t ts, char* out) {
    uint32_t days = ts / 86400;
    uint32_t rem  = ts % 86400;
    uint32_t hour = rem / 3600;
    uint32_t min  = (rem % 3600) / 60;

    uint32_t year = 1970;
    while (true) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        uint32_t yday = leap ? 366 : 365;
        if (days < yday) break;
        days -= yday;
        year++;
    }

    static const uint32_t mdays[]    = {31,28,31,30,31,30,31,31,30,31,30,31};
    static const uint32_t mdays_lp[] = {31,29,31,30,31,30,31,31,30,31,30,31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    const uint32_t* md = leap ? mdays_lp : mdays;

    uint32_t month = 0;
    while (month < 12 && days >= md[month]) {
        days -= md[month];
        month++;
    }
    uint32_t day = days + 1;

    int pos = 0;
    out[pos++] = month_names[month][0];
    out[pos++] = month_names[month][1];
    out[pos++] = month_names[month][2];
    out[pos++] = ' ';
    if (day < 10) {
        out[pos++] = ' ';
        out[pos++] = '0' + day;
    } else {
        out[pos++] = '0' + day / 10;
        out[pos++] = '0' + day % 10;
    }
    out[pos++] = ' ';
    out[pos++] = '0' + hour / 10;
    out[pos++] = '0' + hour % 10;
    out[pos++] = ':';
    out[pos++] = '0' + min / 10;
    out[pos++] = '0' + min % 10;
    out[pos] = '\0';
}

static int digit_width(uint32_t val) {
    int w = 0;
    do { w++; val /= 10; } while (val > 0);
    return w;
}

static void int_to_rpad(uint32_t val, int width, char* buf) {
    buf[width] = '\0';
    int pos = width - 1;
    if (val == 0) {
        buf[pos--] = '0';
    } else {
        while (val > 0 && pos >= 0) {
            buf[pos--] = '0' + (val % 10);
            val /= 10;
        }
    }
    while (pos >= 0) {
        buf[pos--] = ' ';
    }
}

static void build_full_path(const char* dir, const char* name, char* out, int out_len) {
    int plen = strlen(dir);
    strncpy(out, dir, out_len - 1);
    if (plen > 0 && dir[plen - 1] != '/') {
        out[plen++] = '/';
    }
    strncpy(out + plen, name, out_len - 1 - plen);
    out[out_len - 1] = '\0';
}

int main(int argc, char** argv) {
    char path[255];
    if (getcwd(path, 255) != 0) {
        return -1;
    }

    bool showall = false;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        showall = true;
    }
    
    dirent my_dirent;
    file_stat fst;
    char full_path[512];

    // === 第一趟：计算 size 最大宽度 ===
    int max_width = 1;
    int fd = opendir(path);
    if (fd == -1) return -1;
    while (readdir(fd, &my_dirent) == 1) {
        if (!showall && (strcmp(my_dirent.name, ".") == 0 ||
                         strcmp(my_dirent.name, "..") == 0))
            continue;

        build_full_path(path, my_dirent.name, full_path, 512);
        if (stat(full_path, &fst) == 0) {
            int w = digit_width(fst.size);
            if (w > max_width) max_width = w;
        }
    }
    closedir(fd);

    // === 第二趟：格式化输出 ===
    fd = opendir(path);
    if (fd == -1) return -1;

    char mode_str[11];
    char time_str[13];
    char size_str[16];

    while (readdir(fd, &my_dirent) == 1) {
        if (!showall && (strcmp(my_dirent.name, ".") == 0 ||
                         strcmp(my_dirent.name, "..") == 0))
            continue;

        build_full_path(path, my_dirent.name, full_path, 512);

        if (stat(full_path, &fst) != 0) {
            printf("%s\n", my_dirent.name);
            continue;
        }

        mode_to_str(fst.mode, fst.type, mode_str);
        timestamp_to_str(fst.last_modified, time_str);
        int_to_rpad(fst.size, max_width, size_str);

        printf("%s %-8s %-8s %s %s %s\n",
               mode_str,
               fst.owner_name,
               fst.group_name,
               size_str,
               time_str,
               my_dirent.name);
    }

    closedir(fd);
    return 0;
}