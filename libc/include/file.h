#ifndef _FILE_H
#define _FILE_H 1
#include <stdint.h>
#include <pipe.h>
#include <fcntl.h>
#include <stdarg.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2


#define FILE_BUF_SIZE 1024
#define FOPEN_MAX     16
#define EOF           (-1)

typedef struct _FILE {
    int      fd;           // 底层文件描述符
    uint8_t  buf[FILE_BUF_SIZE];
    uint32_t buf_pos;      // 当前读取位置
    uint32_t buf_len;      // 缓冲区中有效数据量
    uint8_t  flags;        // 状态标志
    uint8_t  mode;         // 'r', 'w', 'a'
} FILE;

#define _F_EOF   0x01
#define _F_ERR   0x02
#define _F_READ  0x04
#define _F_WRITE 0x08

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

static FILE _file_pool[FOPEN_MAX];

typedef struct {
    uint32_t size;
    uint8_t  type;
    uint32_t mode;
    uint32_t owner_id;
    uint32_t group_id;
    uint32_t last_modified;
    char     owner_name[32];
    char     group_name[32];
    char     name[100];
    char     link_name[100];
} file_stat;

int stat(const char* path, file_stat* stat);
int mount(uint32_t driver, const char* mount_path, void* device_data);
int unmount(const char* mount_path);
int open(const char* path, uint8_t mode);
int read(int fd, char* buffer, uint32_t size);
int write(int fd, const char* buffer, uint32_t size);
int close(int fd);
int exec(void* code, uint32_t code_size, int argc, char** argv,
    fd_remap* remaps, int remap_cnt);
int unlink(const char* path);
int lseek(int fd, int32_t offset, int whence);

typedef struct {
    char     name[256];
    uint32_t inode;
    uint8_t  type;
} dirent;

int opendir(const char* path);
int readdir(int fd, dirent* out);
int closedir(int fd);
int mkdir(const char* path, int mode);
int chdir(const char* path);
int getcwd(char* buf, uint32_t size);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);

static int _f_fill_buf(FILE *f);
size_t fread(void *ptr, size_t size, size_t count, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f);
int fflush(FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);

void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);

int fgetc(FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fprintf(FILE *f, const char *fmt, ...);

int rename(const char *old, const char *new_);
int remove(const char *path);

int vfprintf(FILE *f, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
