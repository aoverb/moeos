#include <driver/procfs.hpp>
#include <unordered_map>
#include <string>
#include <errno.h>
#include <kernel/mm.hpp>
#include <string.h>
#include <stdio.h>

fs_operation proc_fs_operation;
constexpr char TYPE_DIR = '5';

struct proc_table_entry;
struct proc_table {
    std::unordered_map<uint32_t, proc_table_entry*> entry;
    proc_table_entry* root;
    uint32_t cnt = 0;
};

struct proc_table_entry {
    // 约束：如果不是目录，读取时直接读infos[info]
    // 如果是目录，遍历infos项
    union Data {
        struct { std::unordered_map<std::string, proc_table_entry*> infos; } dir;
        struct { proc_operation* opr; } file;

        Data() {}
        ~Data() {}
    } data;
    proc_table* table;
    char name[32];
    uint32_t inode_id;
    bool is_dir;
    proc_table_entry* parent;

    proc_table_entry(bool is_dir, uint32_t inode_id, const char name[]) : is_dir(is_dir), inode_id(inode_id) {
        strncpy(this->name, name, 32);
        this->name[31] = '\0';
        if (is_dir) {
            new (&data.dir.infos) std::unordered_map<std::string, proc_table_entry*>();
        } else {
            data.file.opr = nullptr;
        }
        this->parent = nullptr;
    }

    ~proc_table_entry() {
        --(table->cnt);
        if (parent) {
            parent->data.dir.infos.erase(name);
        }
        if (is_dir) {
            for (auto& info : data.dir.infos) {
                info.second->parent = nullptr;
                info.second->~proc_table_entry();
                kfree(info.second);
            }
            data.dir.infos.~unordered_map();
        }
        table->entry.erase(inode_id);
    }
};

static int mount(mounting_point* mp) {
    if (!mp) return -1;
    mp->data = new ((proc_table*)kmalloc(sizeof(proc_table))) proc_table();
    auto* table = reinterpret_cast<proc_table*>(mp->data);
    table->root =
        new ((proc_table_entry*)kmalloc(sizeof(proc_table_entry))) proc_table_entry(true, 0, "/");
    table->cnt = 1;
    table->entry[0] = table->root;
    table->root->table = table;
    table->root->parent = nullptr;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path,  uint8_t) {
    ++path; // 第一位是斜线
    if (strlen(path) >= 256) return -ENOENT;
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    char token[256];
    uint32_t token_length = 0;
    proc_table_entry* cur = table->root;
    for (int i = 0; i < strlen(path); ++i) {
        if (path[i] != '/') {
            token[token_length++] = path[i];
            continue;
        }
        token[token_length] = '\0';
        token_length = 0;
        if (!cur->is_dir) return -ENOENT;
        if (cur->data.dir.infos.find(token) == cur->data.dir.infos.end()) {
            return -ENOENT;
        }
        cur = cur->data.dir.infos[token];
    }
    token[token_length] = '\0';
    if (token_length == 0) return cur->inode_id;
    if (cur->data.dir.infos.find(token) == cur->data.dir.infos.end()) {
        return -ENOENT;
    }
    if (!cur->is_dir) return -ENOENT;
    cur = cur->data.dir.infos[token];

    return cur->inode_id;
}

static int close(mounting_point*, uint32_t, uint32_t) {
    return 0;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    const auto& entry_map = reinterpret_cast<proc_table*>(mp->data)->entry;
    if (entry_map.find(inode_id) == entry_map.end()) return -ENOENT;
    proc_table_entry* entry = table->entry[inode_id];
    if (entry->is_dir) return -EISDIR;
    if (!entry->data.file.opr) return -EFAULT;
    return entry->data.file.opr->read(buffer, offset, size);
}

static int write(mounting_point* mp, uint32_t inode_id, uint32_t /* offset */, const char* buffer, uint32_t size) {
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    const auto& entry_map = reinterpret_cast<proc_table*>(mp->data)->entry;
    if (entry_map.find(inode_id) == entry_map.end()) return -ENOENT;
    proc_table_entry* entry = table->entry[inode_id];
    if (entry->is_dir) return -EISDIR;
    if (!entry->data.file.opr) return -EFAULT;
    return entry->data.file.opr->write(buffer, size);
}

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    ++path; // 第一位是斜线
    if (strlen(path) >= 256) return -ENOENT;
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    char token[256];
    uint32_t token_length = 0;
    proc_table_entry* cur = table->root;
    for (int i = 0; i < strlen(path); ++i) {
        if (path[i] != '/') {
            token[token_length++] = path[i];
            continue;
        }
        token[token_length] = '\0';
        token_length = 0;
        if (!cur->is_dir) return -ENOENT;
        if (cur->data.dir.infos.find(token) == cur->data.dir.infos.end()) {
            return -ENOENT;
        }
        cur = cur->data.dir.infos[token];
    }
    token[token_length] = '\0';
    if (token_length != 0) {
        if (cur->data.dir.infos.find(token) == cur->data.dir.infos.end()) {
            return -ENOENT;
        }
        cur = cur->data.dir.infos[token];
    }
    strncpy(out->name, cur->name, 32);
    out->type = !(cur->is_dir);
    out->size = 256;
    out->owner_id = 0;
    out->group_id = 0;
    strcpy(out->owner_name, "root");
    strcpy(out->group_name, "root");
    strcpy(out->link_name, "");
    out->last_modified = 0;

    // 根据设备实际能力设置权限
    bool can_read  = ((cur->is_dir) || (cur->data.file.opr && cur->data.file.opr->read != nullptr));
    bool can_write = (!(cur->is_dir) && cur->data.file.opr && cur->data.file.opr->write != nullptr);
    out->mode = 0;
    if (can_read)  out->mode |= 0444;  // r--r--r--
    if (can_write) out->mode |= 0222;  // -w--w--w-
    return 0;
}

static int ioctl(mounting_point* mp, uint32_t inode_id, uint32_t request, void* arg) {
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    const auto& entry_map = reinterpret_cast<proc_table*>(mp->data)->entry;
    if (entry_map.find(inode_id) == entry_map.end()) return -ENOENT;
    proc_table_entry* entry = table->entry[inode_id];
    if (entry->is_dir) return -EISDIR;
    if (!entry->data.file.opr) return -EFAULT;
    return entry->data.file.opr->ioctl(request, arg);
}

static int opendir(mounting_point* mp, const char* path) {
    return open(mp, path, O_RDONLY);
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    if (!mp->data) return -1;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    const auto& entry_map = reinterpret_cast<proc_table*>(mp->data)->entry;
    if (entry_map.find(inode_id) == entry_map.end()) return -ENOENT;
    proc_table_entry* entry = table->entry[inode_id];
    if (!entry->is_dir) return -ENOTDIR;
    int cur_offset = 0;
    for (const auto& file : entry->data.dir.infos) {
        if (cur_offset++ == offset) {
            out->inode = file.second->inode_id;
            strncpy(out->name, file.second->name, 32);
            out->type = file.second->is_dir ? TYPE_DIR : '?';
            return 1;
        }
    }
    return 0;
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

int register_info_in_procfs(mounting_point* mp, const char* path, const char* info_name,
    proc_operation* opr, bool is_dir) {
    if (!mp->data) return -1;
    int dir_inode_id = opendir(mp, path);
    if (dir_inode_id < 0) return dir_inode_id;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    int new_ent_id = table->cnt;
    while(table->entry.find(new_ent_id) != table->entry.end()) ++new_ent_id;
    auto& dir_entry = table->entry[dir_inode_id];

    table->entry[new_ent_id] = new ((proc_table_entry*)kmalloc(sizeof(proc_table_entry)))
        proc_table_entry(is_dir, new_ent_id, info_name);
    if (!is_dir) {
        table->entry[new_ent_id]->data.file.opr = opr;
    }
    table->entry[new_ent_id]->parent = dir_entry;
    table->entry[new_ent_id]->table = table;
    dir_entry->data.dir.infos[info_name] = table->entry[new_ent_id];
    table->cnt++;
    return new_ent_id;
}

void delete_from_procfs(mounting_point* mp, uint32_t inode_id) {
    if (!mp->data) return;
    proc_table* table = reinterpret_cast<proc_table*>(mp->data);
    const auto& entry_map = reinterpret_cast<proc_table*>(mp->data)->entry;
    if (entry_map.find(inode_id) == entry_map.end()) return;
    table->entry[inode_id]->~proc_table_entry();
    kfree(table->entry[inode_id]);
}
