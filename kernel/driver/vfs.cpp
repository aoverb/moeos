#include <driver/vfs.h>
#include <kernel/panic.h>
#include <kernel/mm.h>
#include <kernel/mm.h>
#include <string.h>
#include <format.h>
#include <stdio.h>

fs_operation* fs_operations[MAX_DRIVER_NUM];

int register_fs_operation(FS_DRIVER driver, fs_operation* operations) {
    uint8_t driver_index = (uint8_t)driver;
    if (fs_operations[driver_index]) return -1;
    fs_operations[driver_index] = operations;
    return 0;
}

fs_operation* get_fs_operation(FS_DRIVER driver) {
    uint8_t driver_index = (uint8_t)driver;
    if (driver_index < 0 || driver_index >= MAX_DRIVER_NUM) return nullptr;
    return fs_operations[driver_index];
}

constexpr uint32_t MAX_MOUNT_NUM = 32;

mounting_point* mount_list[MAX_MOUNT_NUM];
uint32_t mount_num = 0;

mounting_point* get_mounting_point(const char* path) {
    mounting_point* best = nullptr;
    uint32_t best_len = 0;

    for (uint32_t i = 0; i < mount_num; i++) {
        const char* mp_path = mount_list[i]->mount_path;
        uint32_t mp_len = strlen(mp_path);

        // 检查 path 是否以这个挂载路径为前缀
        if (strncmp(path, mp_path, mp_len) == 0) {
            // 确保是完整的路径边界匹配
            // 比如挂载点 "/mnt" 不应该匹配 "/mnt2/file"
            if (mp_len == 1 && mp_path[0] == '/') {
                // 根挂载点 "/" 匹配一切
            } else if (path[mp_len] == '/' || path[mp_len] == '\0') {
                // 合法边界
            } else {
                continue;
            }

            if (mp_len > best_len) {
                best_len = mp_len;
                best = mount_list[i];
            }
        }
    }

    return best;
}

const char* get_mounting_relative_path(mounting_point* mp, const char* path) {
    uint32_t mp_len = strlen(mp->mount_path);
    if (mp_len == 1 && mp->mount_path[0] == '/') {
        return path;
    }

    const char* rel = path + mp_len;

    if (*rel == '\0') {
        return "/";
    }

    return rel;
}

void init_vfs() {
    memset(mount_list, 0, sizeof(mount_list));
    memset(fs_operations, 0, sizeof(fs_operations));
}

int v_mount(FS_DRIVER driver, const char* mount_path, void* device_data) {
    mount_list[mount_num] = reinterpret_cast<mounting_point*>(kmalloc(sizeof(mounting_point)));
    mount_list[mount_num]->operations = get_fs_operation(driver);
    mount_list[mount_num]->index = mount_num;
    mount_list[mount_num]->driver = driver;
    mount_list[mount_num]->data = device_data;
    strcpy(mount_list[mount_num]->mount_path, mount_path);
    if (!mount_list[mount_num]->operations ||
        mount_list[mount_num]->operations->mount(mount_list[mount_num]) != 0) {
        kfree(mount_list[mount_num]);
        mount_list[mount_num] = nullptr;
        return -1;
    }
    
    return mount_num++;
}

int v_unmount(const char* mount_path) {
    for (uint32_t i = 0; i < mount_num; i++) {
        if (strcmp(mount_list[i]->mount_path, mount_path) == 0) { // 精确匹配
            int ret = mount_list[i]->operations->unmount(mount_list[i]);
            if (ret == 0) {
                kfree(mount_list[i]);
                mount_list[i] = nullptr;
            }
            return ret;
        }
    }
    return -1;
}

int alloc_fd(PCB* proc) {
    for (int i = 0; i < MAX_FD_NUM; i++) {
        if (!proc->fd[i].mp) return i;
    }
    return -1;
}

int v_open(PCB* proc, const char* path, uint8_t mode) {
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    uint32_t handle_id = mp->operations->open(mp, get_mounting_relative_path(mp, path), mode);
    if (handle_id == -1) return -1;
    
    int fd_id = alloc_fd(proc);
    if (fd_id == -1) {
        mp->operations->close(mp, handle_id);
        return -1;
    }
    file_description& fd = proc->fd[fd_id];
    strcpy(fd.path, path);
    fd.handle_id = handle_id;
    fd.mp = mp;
    proc->fd_num++;
    return fd_id;
}

int v_read(PCB* proc, int fd, char* buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_FD_NUM) return -1;
    mounting_point* mp = proc->fd[fd].mp;
    if (!mp) return -1;
    return mp->operations->read(mp, proc->fd[fd].handle_id, buffer, size);
}

int v_write(PCB* proc, int fd, const char* buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_FD_NUM) return -1;
    mounting_point* mp = proc->fd[fd].mp;
    if (!mp) return -1;
    return mp->operations->write(mp, proc->fd[fd].handle_id, buffer, size);
}

int v_close(PCB* proc, int fd) {
    if (fd < 0 || fd >= MAX_FD_NUM) return -1;
    mounting_point* mp = proc->fd[fd].mp;
    if (!mp) return -1;
    int ret = mp->operations->close(mp, proc->fd[fd].handle_id);
    if (ret == 0) {
        proc->fd[fd].mp = nullptr;
        proc->fd_num--;
    }
    return ret;
}

int v_opendir(PCB* proc, const char* path) {
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    uint32_t handle_id = mp->operations->opendir(mp, get_mounting_relative_path(mp, path));
    if (handle_id == -1) return -1;
    
    if (proc->fd_num >= MAX_FD_NUM) {
        mp->operations->close(mp, handle_id);
    }
    file_description& fd = proc->fd[proc->fd_num++];
    strcpy(fd.path, path);
    fd.handle_id = handle_id;
    fd.mp = mp;
    return proc->fd_num - 1;
}

int v_readdir(PCB* proc, int fd, dirent* out) {
    if (fd < 0 || fd >= MAX_FD_NUM) return -1;
    mounting_point* mp = proc->fd[fd].mp;
    if (!mp) return -1;
    int ret = mp->operations->readdir(mp, proc->fd[fd].handle_id, out);
    return ret;
}

int v_closedir(PCB* proc, int fd) {
    if (fd < 0 || fd >= MAX_FD_NUM) return -1;
    mounting_point* mp = proc->fd[fd].mp;
    if (!mp) return -1;
    return mp->operations->closedir(mp, proc->fd[fd].handle_id);
}

int v_stat(const char* path, file_stat* out) {
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    return mp->operations->stat(mp, get_mounting_relative_path(mp, path), out);
}

void resolve_path(const char* cwd, const char* input, char* output) {
    char tmp[MAX_PATH_LEN];

    // 1. 拼接：相对路径拼上 cwd，绝对路径直接用
    if (input[0] == '/') {
        strcpy(tmp, input);
    } else {
        uint32_t cwd_len = strlen(cwd);
        strcpy(tmp, cwd);
        if (cwd_len > 1 || cwd[0] != '/') {
            if (tmp[cwd_len - 1] != '/') {
                tmp[cwd_len] = '/';
                tmp[cwd_len + 1] = '\0';
            }
        }
        strcat(tmp, input);
    }

    // 2. 按 '/' 分割，逐段处理 "." 和 ".."
    char* components[MAX_PATH_LEN / 2];
    int depth = 0;

    char* token = tmp;
    while (*token) {
        while (*token == '/') token++;
        if (*token == '\0') break;

        char* end = token;
        while (*end && *end != '/') end++;

        char saved = *end;
        *end = '\0';

        if (strcmp(token, ".") == 0) {
            // 当前目录，跳过
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) depth--;
        } else {
            components[depth++] = token;
        }

        // 不恢复截断，让每个 component 保持独立的 '\0' 结尾
        // 根据原字符决定如何推进 token
        token = saved ? end + 1 : end;
    }

    if (depth == 0) {
        strcpy(output, "/");
        return;
    }

    output[0] = '\0';
    for (int i = 0; i < depth; i++) {
        strcat(output, "/");
        strcat(output, components[i]);
    }
}