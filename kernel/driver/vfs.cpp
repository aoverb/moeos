#include <driver/vfs.hpp>
#include <kernel/panic.h>
#include <kernel/mm.hpp>
#include <kernel/spinlock.hpp>
#include <string.h>
#include <format.h>
#include <stdio.h>

static spinlock vfs_lock;

fs_operation* fs_operations[MAX_DRIVER_NUM];

constexpr uint32_t MAX_HANDLE_NUM = 4096;
static uint32_t file_handle_num = 0;
file_description* file_handle[MAX_HANDLE_NUM];

int register_fs_operation(FS_DRIVER driver, fs_operation* operations) {
    SpinlockGuard guard(vfs_lock);
    uint32_t driver_index = (uint8_t)driver;
    if (fs_operations[driver_index]) return -1;
    fs_operations[driver_index] = operations;
    return 0;
}

fs_operation* get_fs_operation(FS_DRIVER driver) {
    uint32_t driver_index = (uint8_t)driver;
    if (driver_index >= MAX_DRIVER_NUM) return nullptr;
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
    memset(file_handle, 0, sizeof(file_handle));
    file_handle_num = 0;
}

mounting_point* v_mount(FS_DRIVER driver, const char* mount_path, void* device_data) {
    SpinlockGuard guard(vfs_lock);
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
        return nullptr;
    }
    
    return mount_list[mount_num++];
}

int v_unmount(const char* mount_path) {
    SpinlockGuard guard(vfs_lock);
    for (uint32_t i = 0; i < mount_num; i++) {
        if (strcmp(mount_list[i]->mount_path, mount_path) == 0) { // 精确匹配
            int ret = mount_list[i]->operations->unmount(mount_list[i]);
            if (ret == 0) {
                kfree(mount_list[i]);
                mount_list[i] = nullptr;
                --mount_num;
            }
            return ret;
        }
    }
    return -1;
}

int alloc_fd_for_proc(PCB* proc) {
    for (int i = 0; i < MAX_FD_NUM; i++) {
        if (!proc->fd[i]) return i;
    }
    return -1;
}

int get_empty_handle() {
    for (int i = 0; i < MAX_HANDLE_NUM; i++) {
        if (!file_handle[i]) return i;
    }
    return -1;
}

int v_open(PCB* proc, const char* path, uint8_t mode) {
    SpinlockGuard guard(vfs_lock);
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    uint32_t inode_id = mp->operations->open(mp, get_mounting_relative_path(mp, path), mode);
    if (inode_id == -1) return -1;
    
    int fd_pos = alloc_fd_for_proc(proc);
    if (fd_pos == -1) {
        mp->operations->close(mp, inode_id, mode);
        return -1;
    }
    int handle_id = get_empty_handle();
    if (handle_id == -1) {
        mp->operations->close(mp, inode_id, mode);
        return -1;
    }
    file_description*& fd = proc->fd[fd_pos];
    fd = file_handle[handle_id] = (file_description*)kmalloc(sizeof(file_description));
    fd->mp = mp;
    fd->handle_id = handle_id;
    fd->inode_id = inode_id;
    fd->mode = mode;
    fd->offset = 0;
    fd->refcnt = 1;
    strcpy(fd->path, path);
    ++file_handle_num;
    proc->fd_num++;
    return fd_pos;
}

int v_read(PCB* proc, int fd_pos, char* buffer, uint32_t size) {
    mounting_point* mp;
    uint32_t inode_id;
    uint32_t offset;

    {
        SpinlockGuard guard(vfs_lock);
        if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
        {
            SpinlockGuard guard(proc->plock);
            file_description* fd = proc->fd[fd_pos];
            if (!fd || !fd->mp) return -1;
            mp = fd->mp;
            inode_id = fd->inode_id;
            offset = fd->offset;
        }
    }

    int ret = mp->operations->read(mp, inode_id, offset, buffer, size);

    if (ret > 0) {
        SpinlockGuard guard(proc->plock);
        proc->fd[fd_pos]->offset += ret;
    }

    return ret;
}

int v_write(PCB* proc, int fd_pos, const char* buffer, uint32_t size) {
    mounting_point* mp;
    uint32_t inode_id;

    {
        SpinlockGuard guard(vfs_lock);
        if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
        {
            SpinlockGuard guard(proc->plock);
            file_description* fd = proc->fd[fd_pos];
            if (!fd || !fd->mp) return -1;
            mp = fd->mp;
            inode_id = fd->inode_id;
        }
    }

    return mp->operations->write(mp, inode_id, buffer, size);
}

// 调用者必须已持有 vfs_lock
int _v_close(PCB* proc, int fd_pos) {
    file_description* fd = proc->fd[fd_pos];
    if (!fd) return -1;
    proc->fd[fd_pos] = nullptr;
    proc->fd_num--;
    if (--(fd->refcnt) == 0) {
        fd->mp->operations->close(fd->mp, fd->inode_id, fd->mode);
        file_handle[fd->handle_id] = nullptr;
        --file_handle_num;
        kfree(fd);
    }
    return 0;
}

int v_close(PCB* proc, int fd_pos) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    return _v_close(proc, fd_pos);
}

int _v_dup_to(PCB* src_proc, int fd_src, PCB* dst_proc, int fd_dst) {
    // 调用者必须已持有 vfs_lock
    if (fd_src < 0 || fd_src >= MAX_FD_NUM || fd_dst < 0 || fd_dst >= MAX_FD_NUM) return -1;
    file_description* fd_s = src_proc->fd[fd_src];
    if (!fd_s) return -1;
    if (dst_proc->fd[fd_dst]) {
        _v_close(dst_proc, fd_dst);
    }
    dst_proc->fd[fd_dst] = fd_s;
    fd_s->refcnt++;
    dst_proc->fd_num++;
    return 0;
}

int v_dup_to(PCB* src_proc, int fd_src, PCB* dst_proc, int fd_dst) {
    SpinlockGuard guard(vfs_lock);
    return _v_dup_to(src_proc, fd_src, dst_proc, fd_dst);
}

int v_dup(PCB* src_proc, int fd_src, PCB* dst_proc) {
    SpinlockGuard guard(vfs_lock);
    int fd_dst = alloc_fd_for_proc(dst_proc);
    if (fd_dst == -1) return -1;
    _v_dup_to(src_proc, fd_src, dst_proc, fd_dst);
    return fd_dst;
}

int v_opendir(PCB* proc, const char* path) {
    SpinlockGuard guard(vfs_lock);
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    uint32_t inode_id = mp->operations->opendir(mp, get_mounting_relative_path(mp, path));
    if (inode_id == -1) return -1;
    
    int fd_pos = alloc_fd_for_proc(proc);
    if (fd_pos == -1) {
        mp->operations->closedir(mp, inode_id);
        return -1;
    }
    int handle_id = get_empty_handle();
    if (handle_id == -1) {
        mp->operations->closedir(mp, inode_id);
        return -1;
    }
    file_description*& fd = proc->fd[fd_pos];
    fd = file_handle[handle_id] = (file_description*)kmalloc(sizeof(file_description));
    fd->mp = mp;
    fd->handle_id = handle_id;
    fd->inode_id = inode_id;
    fd->offset = 0;
    fd->refcnt = 1;
    strcpy(fd->path, path);
    ++file_handle_num;
    proc->fd_num++;
    return fd_pos;
}

int v_readdir(PCB* proc, int fd_pos, dirent* out) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    file_description*& fd = proc->fd[fd_pos];
    if (!fd) return -1;
    mounting_point* mp = fd->mp;
    if (!mp) return -1;

    uint32_t n_mounts = 0;
    uint32_t dir_len = strlen(fd->path);

    for (uint32_t i = 0; i < mount_num; i++) {
        if (!mount_list[i] || mount_list[i] == mp) continue;
        const char* mpath = mount_list[i]->mount_path;

        // 检查这个挂载点是否是 fd->path 的直接子级
        bool is_child = false;
        if (dir_len == 1 && fd->path[0] == '/') {
            // 当前目录是 "/"：挂载点形如 "/xxx"（无更多 '/'）
            if (mpath[0] == '/' && mpath[1] != '\0' && !strchr(mpath + 1, '/'))
                is_child = true;
        } else {
            // 当前目录是 "/home" 等：挂载点必须是 "/home/xxx"
            if (strncmp(mpath, fd->path, dir_len) == 0 
                && mpath[dir_len] == '/'
                && mpath[dir_len + 1] != '\0'
                && !strchr(mpath + dir_len + 1, '/'))
                is_child = true;
        }

        if (!is_child) continue;

        if (n_mounts == fd->offset) {
            // 提取最后一段名字
            const char* name = (dir_len == 1) ? mpath + 1 : mpath + dir_len + 1;
            strcpy(out->name, name);
            out->type = '5';
            out->inode = 0;
            fd->offset++;
            return 1;
        }
        n_mounts++;
    }

    uint32_t fs_offset = fd->offset - n_mounts;
    int ret = mp->operations->readdir(mp, fd->inode_id, fs_offset, out);
    if (ret == 1) fd->offset++;
    return ret;
}

int v_closedir(PCB* proc, int fd_pos) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    file_description*& fd = proc->fd[fd_pos];
    mounting_point* mp = fd->mp;
    if (!mp) return -1;
    int ret = mp->operations->closedir(mp, fd->inode_id);
    if (ret == 0 && --(fd->refcnt) == 0) {
        uint32_t handle_id = fd->handle_id;
        kfree(file_handle[handle_id]);
        file_handle[handle_id] = nullptr;
        --file_handle_num;
        fd = nullptr;
        proc->fd_num--;
    }
    return ret;
}

int v_stat(const char* path, file_stat* out) {
    SpinlockGuard guard(vfs_lock);
    mounting_point* mp = get_mounting_point(path);
    if (!mp) return -1;
    return mp->operations->stat(mp, get_mounting_relative_path(mp, path), out);
}

int v_ioctl(PCB* proc, int fd_pos, const char* cmd, void* arg) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    file_description*& fd = proc->fd[fd_pos];
    if (!fd) return -1;
    mounting_point* mp = fd->mp;
    if (!mp || !(mp->operations->ioctl)) return -1;
    return mp->operations->ioctl(mp, fd->inode_id, cmd, arg);
}

int v_connect(PCB* proc, int fd_pos, const char* addr, uint16_t port) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    file_description*& fd = proc->fd[fd_pos];
    if (!fd) return -1;
    mounting_point* mp = fd->mp;
    if (!mp || !(mp->operations->sock_opr)) return -1;
    return mp->operations->sock_opr->connect(mp, fd->inode_id, addr, port);
}

void resolve_path(const char* cwd, const char* input, char* output) {
    char tmp[MAX_PATH_LEN];

    if (input[0] == '/') {
        strcpy(tmp, input);
    } else {
        strcpy(tmp, cwd);
        uint32_t len = strlen(tmp);
        if (len > 0 && tmp[len - 1] != '/') {
            tmp[len] = '/';
            tmp[len + 1] = '\0';
        }
        strcat(tmp, input);
    }

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
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) depth--;
        } else {
            components[depth++] = token;
        }

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
