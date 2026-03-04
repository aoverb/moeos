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

struct pipe_data {
    pipe_entry* entry[MAX_PIPE_NUM];
    uint32_t pipe_cnt = 0;
};

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

    fds[0] = v_open(proc, "/pipe/create", O_CREATE | O_RDONLY); // 有点像RESTful
    printf("fds[0]: %d\n", fds[0]);
    if (fds[0] < 0) return -1;
    int pipe_idx = proc->fd[fds[0]]->inode_id;
    char path[32];
    sprintf(path, "/pipe/%d", pipe_idx);
    fds[1] = v_open(proc, path, O_WRONLY);
    printf("fds[1]: %d\n", fds[1]);
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
    printf("pipe_open: %s\n", path);
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    printf("pipe_open: %s\n", path);
    if (strcmp("create", path) == 0) {
        pipe_entry* new_pipe = alloc_entry(data);
        if (!new_pipe) return -1;
        if (mode & O_RDONLY) {
            new_pipe->read_open = true;
        }
        if (mode & O_WRONLY) {
            new_pipe->write_open = true;
        }
        return new_pipe->entry_id;
    } else {
        int idx = atoi(path);
        if (idx < 0 || !data->entry[idx]) return -1;
        return idx;
    }
    return -1;
}

static int close(mounting_point* mp, uint32_t inode_id, uint32_t mode) {
    if (!mp->data) return -1;
    pipe_data* data = reinterpret_cast<pipe_data*>(mp->data);
    if (!data->entry[inode_id]) return -1;
    pipe_entry* pipe = data->entry[inode_id];
    if (mode & O_RDONLY) {
        pipe->read_open = false;
        PCB* proc;
        while (proc = pipe->writer) {
            proc->state = process_state::READY;
            remove_from_process_queue(pipe->writer, proc->pid);
            insert_into_scheduling_queue(proc->pid);
        }
    }
    if (mode & O_WRONLY) {
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
    for (int i = 0; i < size; ++i) {
        while (pipe->write_pos == (pipe->write_pos + 1) % PIPE_BUF_SIZE) { // 缓冲区已满
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
        pipe->read_pos = pipe->read_pos % PIPE_BUF_SIZE;
    }
    return 0;
}

static int stat(mounting_point*, const char*, file_stat*) {
    return -1;
}

static int opendir(mounting_point*, const char*) {
    return 0;
}

static int readdir(mounting_point*, uint32_t, uint32_t, dirent*) {
    return 0;
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
