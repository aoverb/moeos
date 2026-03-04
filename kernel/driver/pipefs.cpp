#include <driver/vfs.hpp>
#include <driver/pipefs.hpp>
#include <kernel/schedule.h>
#include <unordered_map>

#include <kernel/mm.h>
#include <string.h>
#include <stdio.h>
#include <format.h>

#include <pipe.h>

fs_operation pipe_fs_operation;

constexpr uint32_t MAX_PIPE_NUM = 64;
constexpr uint32_t PIPE_BUF_SIZE = 4096;

struct pipe_entry {
    char buffer[PIPE_BUF_SIZE];
    uint32_t entry_id;
    uint32_t read_pos;
    uint32_t write_pos;
    uint8_t read_open;
    uint8_t write_open;
    process_queue reader;
    process_queue writer;
};

typedef struct pipe_data {
    pipe_entry* entry[MAX_PIPE_NUM];
    uint32_t pipe_cnt = 0;
} pipe_data;

static inline bool is_read_mode(uint8_t mode) {
    return !(mode & O_WRONLY);  // 不是写就是读
}

static inline bool is_write_mode(uint8_t mode) {
    return (mode & O_WRONLY);
}


pipe_entry* alloc_entry(pipe_data* data) {
    for (int i = 0; i < MAX_PIPE_NUM; ++i) {
        if (!data->entry[i]) {
            data->entry[i] = (pipe_entry*)kmalloc(sizeof(pipe_entry));
            memset(data->entry[i], 0, sizeof(pipe_entry));
            data->entry[i]->entry_id = i;
            ++(data->pipe_cnt);
            return data->entry[i];
        }
    }
    return nullptr;
}

int kpipe(int fds[2]) {
    PCB* proc = process_list[cur_process_id];

    fds[0] = v_open(proc, "/pipe/create", O_RDONLY); // 有点像RESTful
    if (fds[0] < 0) return -1;
    int pipe_idx = proc->fd[fds[0]]->inode_id;
    char path[32];
    sprintf(path, "/pipe/%d", pipe_idx);
    fds[1] = v_open(proc, path, O_WRONLY);
    if (fds[1] < 0) {
        v_close(proc, fds[0]);
        return -1;
    }
    return 0;
}

static int mount(mounting_point* mp) {
    if (!mp) return -1;
    mp->data = (pipe_data*)kmalloc(sizeof(pipe_data));
    memset(mp->data, 0, sizeof(pipe_data));
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path,  uint8_t mode) {
    ++path; // 第一位是斜线
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    if (strcmp("create", path) == 0) {
        pipe_entry* new_pipe = alloc_entry(data);
        if (!new_pipe) return -1;
        if (is_read_mode(mode)) {
            new_pipe->read_open = true;
        }
        if (is_write_mode(mode)) {
            new_pipe->write_open = true;
        }
        return new_pipe->entry_id;
    } else {
        int idx = atoi(path);
        if (idx < 0 || idx >= MAX_PIPE_NUM || !data->entry[idx]) return -1;
        if (is_read_mode(mode)) data->entry[idx]->read_open = true;
        if (is_write_mode(mode)) data->entry[idx]->write_open = true;
        return idx;
    }
    return -1;
}

static int close(mounting_point* mp, uint32_t inode_id, uint32_t mode) {
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    if (!data->entry[inode_id]) return -1;
    pipe_entry* pipe = data->entry[inode_id];
    if (is_read_mode(mode)) {
        pipe->read_open = false;
        PCB* proc;
        while (proc = pipe->writer) {
            proc->state = process_state::READY;
            remove_from_process_queue(pipe->writer, proc->pid);
            insert_into_scheduling_queue(proc->pid);
        }
    }
    if (is_write_mode(mode)) {
        pipe->write_open = false;
        PCB* proc;
        while (proc = pipe->reader) {
            proc->state = process_state::READY;
            remove_from_process_queue(pipe->reader, proc->pid);
            insert_into_scheduling_queue(proc->pid);
        }
    }
    if (!pipe->read_open && !pipe->write_open) {
        kfree(data->entry[inode_id]);
        data->entry[inode_id] = nullptr;
        --(data->pipe_cnt);
    }
    return 0;
}

// 管道是字节流，没有偏移这种概念
static int read(mounting_point* mp, uint32_t inode_id, uint32_t /* offset */, char* buffer, uint32_t size) {
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    if (!data->entry[inode_id]) return -1;
    pipe_entry* pipe = data->entry[inode_id];
    uint32_t read_cnt = 0;
    for (int i = 0; i < size; ++i) {
        while (pipe->read_pos == pipe->write_pos) { // 当前没有可读的字节
            if (!pipe->write_open) return read_cnt;
            PCB* proc;
            while (proc = pipe->writer) {
                proc->state = process_state::READY;
                remove_from_process_queue(pipe->writer, proc->pid);
                insert_into_scheduling_queue(proc->pid);
            }
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(pipe->reader, process_list[cur_process_id]);
            yield();
        }
        buffer[i] = pipe->buffer[(pipe->read_pos++)];
        pipe->read_pos = pipe->read_pos % PIPE_BUF_SIZE;
        ++read_cnt;
    }
    return read_cnt;
}

static int write(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size) {
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    if (!data->entry[inode_id]) return -1;
    pipe_entry* pipe = data->entry[inode_id];
    uint32_t write_cnt = 0;
    for (int i = 0; i < size; ++i) {
        while (pipe->read_pos == (pipe->write_pos + 1) % PIPE_BUF_SIZE) { // 缓冲区已满
            if (!pipe->read_open) return write_cnt;
            PCB* proc;
            while (proc = pipe->reader) {
                proc->state = process_state::READY;
                remove_from_process_queue(pipe->reader, proc->pid);
                insert_into_scheduling_queue(proc->pid);
            }
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(pipe->writer, process_list[cur_process_id]);
            yield();
        }
        pipe->buffer[(pipe->write_pos++)] = buffer[i];
        pipe->write_pos = pipe->write_pos % PIPE_BUF_SIZE;
        ++write_cnt;
    }
    return write_cnt;
}

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    if (!mp->data) return -1;
    if (path[0] == '/') path++;

    if (path[0] == '\0') {
        strcpy(out->name, "dev");
        out->type = 0;
        out->mode = 0755;
        out->size = 0;
        out->owner_id = 0;
        out->group_id = 0;
        strcpy(out->owner_name, "root");
        strcpy(out->group_name, "root");
        strcpy(out->link_name, "");
        out->last_modified = 0;
        return 0;
    }

    pipe_data* item = reinterpret_cast<pipe_data*>(mp->data);
    if (item->entry[atoi(path)]) {
        strcpy(out->name, path);
        out->type = 1;
        out->size = 0;
        out->owner_id = 0;
        out->group_id = 0;
        strcpy(out->owner_name, "root");
        strcpy(out->group_name, "root");
        strcpy(out->link_name, "");
        out->last_modified = 0;

        // 根据设备实际能力设置权限
        bool can_read  = (item->entry[atoi(path)]->read_open);
        bool can_write = (item->entry[atoi(path)]->write_open);
        out->mode = 0;
        if (can_read)  out->mode |= 0444;  // r--r--r--
        if (can_write) out->mode |= 0222;  // -w--w--w-
        return 0;
    }
    return -1;
}

static int opendir(mounting_point*, const char*) {
    return 0;
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    if (inode_id != 0) return 0;
    if (!mp->data) return 0;
    pipe_data* item = reinterpret_cast<pipe_data*>(mp->data);

    while (offset < MAX_PIPE_NUM && !item->entry[offset]) {
        ++offset;
    }

    if (offset >= MAX_PIPE_NUM) return 0;

    out->inode = offset;
    sprintf(out->name, "pipe_%d", offset);
    out->type = 'p';
    return 1;
}

static int closedir(mounting_point*, uint32_t) {
    return 0;
}

void init_pipefs() {
    pipe_fs_operation.mount = &mount;
    pipe_fs_operation.unmount = &unmount;
    pipe_fs_operation.open = &open;
    pipe_fs_operation.read = &read;
    pipe_fs_operation.write = &write;
    pipe_fs_operation.close = &close;
    pipe_fs_operation.opendir = &opendir;
    pipe_fs_operation.readdir = &readdir;
    pipe_fs_operation.closedir = &closedir;
    pipe_fs_operation.stat = &stat;
    register_fs_operation(FS_DRIVER::PIPEFS, &pipe_fs_operation);
}
