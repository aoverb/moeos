#include <file.h>
#include <poll.h>
#include <string.h>
#include <stdarg.h>
#include <format.h>
#include <unistd.h>
#include <syscall_def.hpp>

static FILE _stdin_file  = { 0, {0}, 0, 0, _F_READ,  'r' };
static FILE _stdout_file = { 1, {0}, 0, 0, _F_WRITE, 'w' };
static FILE _stderr_file = { 1, {0}, 0, 0, _F_WRITE, 'w' };

FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

int poll(pollfd* fds, uint32_t fd_num, uint32_t timeout) {
    return syscall3((uint32_t)SYSCALL::POLL, (uint32_t)fds, (uint32_t)fd_num, (uint32_t)timeout);
}

int stat(const char* path, file_stat* stat) {
    return syscall2((uint32_t)SYSCALL::STAT, (uint32_t)path, (uint32_t)stat);
}

int mount(uint32_t driver, const char* mount_path, void* device_data) {
    return syscall3((uint32_t)SYSCALL::MOUNT, driver, (uint32_t)mount_path, (uint32_t)device_data);
}

int unmount(const char* mount_path) {
    return syscall1((uint32_t)SYSCALL::UNMOUNT, (uint32_t)mount_path);
}

int open(const char* path, uint8_t mode) {
    return syscall2((uint32_t)SYSCALL::OPEN, (uint32_t)path, (uint32_t)mode);
}

int read(int fd, char* buffer, uint32_t size) {
    return syscall3((uint32_t)SYSCALL::READ, (uint32_t)fd, (uint32_t)buffer, size);
}

int write(int fd, const char* buffer, uint32_t size) {
    return syscall3((uint32_t)SYSCALL::WRITE, (uint32_t)fd, (uint32_t)buffer, size);
}

int close(int fd) {
    return syscall1((uint32_t)SYSCALL::CLOSE, (uint32_t)fd);
}

int opendir(const char* path) {
    return syscall1((uint32_t)SYSCALL::OPENDIR, (uint32_t)path);
}

int readdir(int fd, dirent* out) {
    return syscall2((uint32_t)SYSCALL::READDIR, (uint32_t)fd, (uint32_t)out);
}

int getcwd(char* buf, uint32_t size) {
    return syscall2((uint32_t)SYSCALL::GETCWD, (uint32_t)buf, (uint32_t)size);
}

int closedir(int fd) {
    return syscall1((uint32_t)SYSCALL::CLOSEDIR, (uint32_t)fd);
}

int lseek(int fd, int32_t offset, int whence) {
    return syscall3((uint32_t)SYSCALL::LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int chdir(const char* path) {
    return syscall1((uint32_t)SYSCALL::CHDIR, (uint32_t)path);
}

int exec(void* code, uint32_t code_size, int argc, char** argv, fd_remap* remaps, int remap_cnt) {
    return (uint32_t)syscall6((uint32_t)SYSCALL::EXEC,
                              (uint32_t)code,
                              code_size,
                              (uint32_t)argc,
                              (uint32_t)argv,
                              (uint32_t)remaps,
                              (uint32_t)remap_cnt);
}

int unlink(const char* path) {
    return syscall1((uint32_t)SYSCALL::UNLINK, (uint32_t)path);
}

int mkdir(const char* path, int /* mode */) {
    return syscall1((uint32_t)SYSCALL::MKDIR, (uint32_t)path);
}

int pipe(int fds[2]) {
#if defined(__is_libk)
    kpipe(fds);
    // 内核实现...
#else
    return syscall1((uint32_t)SYSCALL::PIPE, (uint32_t)fds);
#endif
}

FILE *fopen(const char *path, const char *mode) {
    int fd_mode;
    uint8_t fflags;

    if (mode[0] == 'r') {
        fd_mode = O_RDONLY;
        fflags = _F_READ;
    } else if (mode[0] == 'w') {
        fd_mode = O_WRONLY | O_CREAT | O_TRUNC;
        fflags = _F_WRITE;
    } else if (mode[0] == 'a') {
        fd_mode = O_WRONLY | O_CREAT | O_APPEND;
        fflags = _F_WRITE;
    } else {
        return NULL;
    }

    // "rb", "wb" 等带 b 的直接忽略，C 里没区别
    // "r+", "w+" 读写模式
    if (mode[1] == '+' || (mode[1] && mode[2] == '+')) {
        fd_mode = O_RDWR | (fd_mode & (O_CREAT | O_TRUNC | O_APPEND));
        fflags = _F_READ | _F_WRITE;
    }

    int fd = open(path, fd_mode);
    if (fd < 0) return NULL;

    // 从池中找空闲 FILE
    for (int i = 0; i < FOPEN_MAX; i++) {
        if (_file_pool[i].fd == 0 && _file_pool[i].flags == 0) {
            _file_pool[i].fd      = fd;
            _file_pool[i].flags   = fflags;
            _file_pool[i].buf_pos = 0;
            _file_pool[i].buf_len = 0;
            return &_file_pool[i];
        }
    }

    close(fd);
    return NULL;
}

int fclose(FILE *f) {
    if (!f) return EOF;
    fflush(f);
    int ret = close(f->fd);
    memset(f, 0, sizeof(FILE));
    return ret;
}

// 从缓冲区填充数据
static int _f_fill_buf(FILE *f) {
    int n = read(f->fd, (char*)f->buf, FILE_BUF_SIZE);
    if (n <= 0) {
        f->flags |= (n == 0) ? _F_EOF : _F_ERR;
        return 0;
    }
    f->buf_pos = 0;
    f->buf_len = n;
    return n;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *f) {
    size_t total = size * count;
    size_t done = 0;
    uint8_t *dst = (uint8_t *)ptr;

    while (done < total) {
        // 先消耗缓冲区
        if (f->buf_pos < f->buf_len) {
            size_t avail = f->buf_len - f->buf_pos;
            size_t chunk = (total - done < avail) ? total - done : avail;
            memcpy(dst + done, f->buf + f->buf_pos, chunk);
            f->buf_pos += chunk;
            done += chunk;
        } else {
            if (_f_fill_buf(f) == 0) break;
        }
    }

    return (size > 0) ? done / size : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f) {
    size_t total = size * count;
    int ret = write(f->fd, (const char *)ptr, total);
    if (ret < 0) {
        f->flags |= _F_ERR;
        return 0;
    }
    return (size > 0) ? ret / size : 0;
}

int fflush(FILE *f) {
    (void)f;  // 没有写缓冲，什么都不做
    return 0;
}

int fseek(FILE *f, long offset, int whence) {
    // 清空读缓冲，否则位置不对
    f->buf_pos = 0;
    f->buf_len = 0;
    f->flags &= ~_F_EOF;

    int ret = lseek(f->fd, offset, whence);
    return (ret < 0) ? -1 : 0;
}

long ftell(FILE *f) {
    long pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    // 实际读取位置要减去缓冲区中还没消耗的部分
    return pos - (f->buf_len - f->buf_pos);
}

void rewind(FILE *f) {
    fseek(f, 0, SEEK_SET);
    f->flags &= ~(_F_EOF | _F_ERR);
}

int feof(FILE *f)   { return (f->flags & _F_EOF) ? 1 : 0; }
int ferror(FILE *f) { return (f->flags & _F_ERR) ? 1 : 0; }

int fgetc(FILE *f) {
    uint8_t c;
    if (fread(&c, 1, 1, f) != 1) return EOF;
    return c;
}

char *fgets(char *s, int n, FILE *f) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f) {
    uint8_t ch = c;
    return (fwrite(&ch, 1, 1, f) == 1) ? c : EOF;
}

int fputs(const char *s, FILE *f) {
    size_t len = strlen(s);
    return (fwrite(s, 1, len, f) == len) ? 0 : EOF;
}

int fprintf(FILE *f, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fwrite(buf, 1, n, f);
    return n;
}

int rename(const char *old, const char *new_) { (void)old; (void)new_; return -1; }
int remove(const char *path) { (void)path; return -1; }

int ftruncate(int fd, uint32_t length) {
    return syscall2((uint32_t)SYSCALL::TRUNCATE, (uint32_t)fd, (uint32_t)length);
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    fwrite(buf, 1, n, f);
    return n;
}