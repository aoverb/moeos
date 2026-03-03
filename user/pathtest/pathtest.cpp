#include <stdio.h>
#include <string.h>
#include <file.h>

static void u32s(uint32_t val, char* out) {
    char tmp[12]; int i = 0;
    if (val == 0) { tmp[i++] = '0'; }
    else { while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; } }
    for (int j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    out[i] = '\0';
}

static void u32oct(uint32_t val, char* out) {
    char tmp[12]; int i = 0;
    if (val == 0) { tmp[i++] = '0'; }
    else { while (val > 0) { tmp[i++] = '0' + (val & 7); val >>= 3; } }
    int p = 0; out[p++] = '0';
    for (int j = i - 1; j >= 0; j--) out[p++] = tmp[j];
    out[p] = '\0';
}

static void u8hex(uint8_t val, char* out) {
    const char* h = "0123456789abcdef";
    out[0] = h[(val >> 4) & 0xF]; out[1] = h[val & 0xF]; out[2] = '\0';
}

static void mstr(uint32_t mode, uint8_t type, char* o) {
    o[0] = (type == 0) ? 'd' : (type == 2) ? 'l' : '-';
    o[1] = (mode & 0400) ? 'r' : '-'; o[2] = (mode & 0200) ? 'w' : '-';
    o[3] = (mode & 0100) ? 'x' : '-'; o[4] = (mode & 040)  ? 'r' : '-';
    o[5] = (mode & 020)  ? 'w' : '-'; o[6] = (mode & 010)  ? 'x' : '-';
    o[7] = (mode & 04)   ? 'r' : '-'; o[8] = (mode & 02)   ? 'w' : '-';
    o[9] = (mode & 01)   ? 'x' : '-'; o[10] = '\0';
}

static void fullpath(const char* dir, const char* name, char* out, int len) {
    int p = strlen(dir);
    strncpy(out, dir, len - 1);
    if (p > 0 && dir[p - 1] != '/') out[p++] = '/';
    strncpy(out + p, name, len - 1 - p);
    out[len - 1] = '\0';
}

static int nok = 0, nfail = 0;
static void T(const char* name, int pass) {
    if (pass) { printf(" OK   %s\n", name); nok++; }
    else      { printf(" FAIL %s\n", name); nfail++; }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: pathtest <path>\n"); return -1; }

    const char* tgt = argv[1];
    char b1[16], b2[16], ms[11], buf[256], full[512];
    file_stat fst;
    dirent ent;
    int fd, ret;

    printf("== pathtest: %s ==\n", tgt);

    /* stat */
    ret = stat(tgt, &fst);
    T("stat", ret == 0);
    if (ret == 0) {
        mstr(fst.mode, fst.type, ms);
        u32oct(fst.mode, b1); u32s(fst.size, b2);
        printf("   %s %s %s sz=%s own=%s grp=%s\n",
               (fst.type==0)?"dir":(fst.type==2)?"lnk":"file",
               ms, b1, b2, fst.owner_name, fst.group_name);
    }

    /* open RDONLY + read */
    fd = open(tgt, O_RDONLY);
    T("open RD", fd >= 0);
    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        ret = read(fd, buf, 128);
        T("read", ret >= 0);
        if (ret > 0) {
            int n = ret < 24 ? ret : 24;
            char hx[4];
            printf("   %d B: ", ret);
            for (int i = 0; i < n; i++) { printf("%c", buf[i]); }
            if (ret > 24) printf("..");
            printf("\n");
        }
        close(fd);
    }

    /* open WRONLY + write */
    fd = open(tgt, O_WRONLY);
    T("open WR", fd >= 0);
    if (fd >= 0) { T("write", write(fd, "probe", 5) > 0); close(fd); }

    /* open RDWR */
    fd = open(tgt, O_RDWR);
    T("open RW", fd >= 0);
    if (fd >= 0) close(fd);

    /* O_CREATE + verify */
    {
        char tmp[512]; int pl = strlen(tgt);
        memset(tmp, 0, sizeof(tmp));
        strncpy(tmp, tgt, sizeof(tmp) - 1);
        if (tmp[pl - 1] == '/') strncpy(tmp + pl, "__tc", sizeof(tmp) - 1 - pl);
        else                    strncpy(tmp + pl, ".__tc", sizeof(tmp) - 1 - pl);
        fd = open(tmp, O_CREATE | O_RDWR);
        T("create", fd >= 0);
        if (fd >= 0) {
            write(fd, "OK", 2); close(fd);
            fd = open(tmp, O_RDONLY);
            if (fd >= 0) {
                memset(buf, 0, 8); read(fd, buf, 2);
                T("create vrfy", strncmp(buf, "OK", 2) == 0);
                close(fd);
            }
        }
    }

    /* opendir + readdir count */
    fd = opendir(tgt);
    T("opendir", fd >= 0);
    if (fd >= 0) {
        int c = 0;
        while (readdir(fd, &ent) == 1) c++;
        closedir(fd);
        u32s((uint32_t)c, b1);
        printf("   entries: %s\n", b1);
    }

    /* chdir */
    {
        char old[256];
        if (getcwd(old, sizeof(old)) == 0) {
            ret = chdir(tgt);
            T("chdir", ret == 0);
            chdir(old);
        }
    }

    /* children (dir only, max 6) */
    if (stat(tgt, &fst) == 0 && fst.type == 0) {
        fd = opendir(tgt);
        if (fd >= 0) {
            file_stat cs; int n = 0;
            while (readdir(fd, &ent) == 1) {
                if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0) continue;
                fullpath(tgt, ent.name, full, (int)sizeof(full));
                if (stat(full, &cs) == 0) {
                    mstr(cs.mode, cs.type, ms); u32s(cs.size, b1);
                    printf("   %s %8s %s\n", ms, b1, ent.name);
                }
                if (++n >= 6) { printf("   ...\n"); break; }
            }
            closedir(fd);
        }
    }

    /* error handling */
    T("stat ''", stat("", &fst) != 0);
    T("stat noexist", stat("/no_xyz", &fst) != 0);
    fd = open("/no_xyz", O_RDONLY);
    T("open noexist", fd < 0); if (fd >= 0) close(fd);
    fd = opendir("/no_xyz");
    T("opendir noex", fd < 0); if (fd >= 0) closedir(fd);
    memset(buf, 0, 4);
    T("read bad fd", read(-1, buf, 1) <= 0);
    T("write bad fd", write(-1, "x", 1) <= 0);

    printf("== %d ok, %d fail ==\n", nok, nfail);
    return (nfail > 0) ? 1 : 0;
}