#include <driver/procfs.hpp>
#include <unordered_map>

#include <kernel/mm.hpp>
#include <string.h>
#include <stdio.h>

fs_operation proc_fs_operation;

constexpr uint32_t MAX_PROC_NUM = 65536;

struct proc_table_entry {
    proc_operation* opr;
    char name[32];
    uint32_t inode_id;
};

struct proc_table {
    proc_table_entry* entry[MAX_PROC_NUM];
    uint32_t cnt = 0;
};

static int return_minus_1(char* buffer, uint32_t offset, uint32_t size) { return -1; }
static int return_minus_1_(const char* buffer, uint32_t size) { return -1; }

static int mount(mounting_point* mp) {
    if (!mp) return -1;
    mp->data = (proc_table*)kmalloc(sizeof(proc_table));
    memset(mp->data, 0, sizeof(proc_table));
    reinterpret_cast<proc_table*>(mp->data)->entry[0] = (proc_table_entry*)kmalloc(sizeof(proc_table_entry));
    reinterpret_cast<proc_table*>(mp->data)->entry[0]->inode_id = 0;
    strcpy(reinterpret_cast<proc_table*>(mp->data)->entry[0]->name, ".");
    reinterpret_cast<proc_table*>(mp->data)->entry[0]->opr = (proc_operation*)kmalloc(sizeof(proc_operation));
    reinterpret_cast<proc_table*>(mp->data)->entry[0]->opr->read = &return_minus_1;
    reinterpret_cast<proc_table*>(mp->data)->entry[0]->opr->write = &return_minus_1_;
    reinterpret_cast<proc_table*>(mp->data)->cnt = 1;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path,  uint8_t) {
    ++path; // 第一位是斜线
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    for (int i = 0; i < table->cnt; ++i) {
        if (table->entry[i]->opr && strcmp(path, table->entry[i]->name) == 0) {
            return i;
        }
    }
    return -1;
}

static int close(mounting_point*, uint32_t, uint32_t) {
    return 0;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    if (!mp->data || (reinterpret_cast<proc_table*>(mp->data)->cnt <= inode_id)) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    return table->entry[inode_id]->opr->read(buffer, offset, size);
}

static int write(mounting_point* mp, uint32_t inode_id, uint32_t offset, const char* buffer, uint32_t size) {
    if (!mp->data || (reinterpret_cast<proc_table*>(mp->data)->cnt <= inode_id)) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    return table->entry[inode_id]->opr->write(buffer, size);
}

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    if (!mp->data) return -1;
    if (path[0] == '/') path++;

    if (path[0] == '\0') {
        strcpy(out->name, "proc");
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

    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    for (uint32_t i = 0; i < table->cnt; ++i) {
        if (table->entry[i]->opr && strcmp(path, table->entry[i]->name) == 0) {
            strcpy(out->name, table->entry[i]->name);
            out->type = 1;
            out->size = 256;
            out->owner_id = 0;
            out->group_id = 0;
            strcpy(out->owner_name, "root");
            strcpy(out->group_name, "root");
            strcpy(out->link_name, "");
            out->last_modified = 0;

            // 根据设备实际能力设置权限
            bool can_read  = (table->entry[i]->opr->read  != nullptr);
            bool can_write = (table->entry[i]->opr->write != nullptr);
            out->mode = 0;
            if (can_read)  out->mode |= 0444;  // r--r--r--
            if (can_write) out->mode |= 0222;  // -w--w--w-
            return 0;
        }
    }

    return -1;
}

static int ioctl(mounting_point* mp, uint32_t inode_id, uint32_t request, void* arg) {
    if (!mp->data || (reinterpret_cast<proc_table*>(mp->data)->cnt <= inode_id)) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    if (table->entry[inode_id]->opr->ioctl == nullptr) return -1;
    return table->entry[inode_id]->opr->ioctl(request, arg);
}

static int opendir(mounting_point*, const char*) {
    return 0;
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    if (inode_id != 0) return 0;
    if (!mp->data || (reinterpret_cast<proc_table*>(mp->data)->cnt <= inode_id)) return 0;
    
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    if (offset >= table->cnt || !table->entry[offset]->opr) return 0;
    out->inode = table->entry[offset]->inode_id;
    strcpy(out->name, table->entry[offset]->name);
    out->type = '?';
    return 1;
}

static int closedir(mounting_point*, uint32_t) {
    return 0;
}

void init_procfs() {
    proc_fs_operation.mount = &mount;
    proc_fs_operation.unmount = &unmount;
    proc_fs_operation.open = &open;
    proc_fs_operation.read = &read;
    proc_fs_operation.write = &write;
    proc_fs_operation.close = &close;
    proc_fs_operation.opendir = &opendir;
    proc_fs_operation.readdir = &readdir;
    proc_fs_operation.closedir = &closedir;
    proc_fs_operation.stat = &stat;
    proc_fs_operation.ioctl = &ioctl;
    proc_fs_operation.sock_opr = nullptr;
    register_fs_operation(FS_DRIVER::PROCFS, &proc_fs_operation);
}

int register_in_procfs(mounting_point* mp, const char* proc_name, proc_operation* opr) {
    if (!mp->data || (reinterpret_cast<proc_table*>(mp->data)->cnt >= MAX_PROC_NUM)) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    table->entry[table->cnt] = (proc_table_entry*)kmalloc(sizeof(proc_table_entry));
    table->entry[table->cnt]->inode_id = table->cnt;
    strcpy(table->entry[table->cnt]->name, proc_name);
    table->entry[table->cnt]->opr = opr;
    return table->cnt++;
}
