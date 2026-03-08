#include <driver/devfs.hpp>
#include <unordered_map>

#include <kernel/mm.hpp>
#include <string.h>
#include <stdio.h>

fs_operation dev_fs_operation;

constexpr uint32_t MAX_DEV_NUM = 64;

struct dev_table_entry {
    dev_operation* opr;
    char name[32];
    uint32_t dev_id;
};

struct dev_item {
    dev_table_entry entry[MAX_DEV_NUM];
    uint32_t devcnt = 0;
};

static int return_minus_1(char* buffer, uint32_t offset, uint32_t size) { return -1; }
static int return_minus_1_(const char* buffer, uint32_t size) { return -1; }

static int mount(mounting_point* mp) {
    if (!mp) return -1;
    mp->data = (dev_item*)kmalloc(sizeof(dev_item));
    memset(mp->data, 0, sizeof(dev_item));
    reinterpret_cast<dev_item*>(mp->data)->entry[0].dev_id = 0;
    strcpy(reinterpret_cast<dev_item*>(mp->data)->entry[0].name, ".");
    reinterpret_cast<dev_item*>(mp->data)->entry[0].opr = (dev_operation*)kmalloc(sizeof(dev_operation));
    reinterpret_cast<dev_item*>(mp->data)->entry[0].opr->read = &return_minus_1;
    reinterpret_cast<dev_item*>(mp->data)->entry[0].opr->write = &return_minus_1_;
    reinterpret_cast<dev_item*>(mp->data)->devcnt = 1;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path,  uint8_t) {
    ++path; // 第一位是斜线
    if (!mp->data) return -1;
    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    for (int i = 0; i < item->devcnt; ++i) {
        if (item->entry[i].opr && strcmp(path, item->entry[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static int close(mounting_point*, uint32_t, uint32_t) {
    return 0;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    if (!mp->data || (reinterpret_cast<dev_item*>(mp->data)->devcnt <= inode_id)) return -1;
    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    return item->entry[inode_id].opr->read(buffer, offset, size);
}

static int write(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size) {
    if (!mp->data || (reinterpret_cast<dev_item*>(mp->data)->devcnt <= inode_id)) return -1;
    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    return item->entry[inode_id].opr->write(buffer, size);
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

    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    for (uint32_t i = 0; i < item->devcnt; ++i) {
        if (item->entry[i].opr && strcmp(path, item->entry[i].name) == 0) {
            strcpy(out->name, item->entry[i].name);
            out->type = 1;
            out->size = 256;
            out->owner_id = 0;
            out->group_id = 0;
            strcpy(out->owner_name, "root");
            strcpy(out->group_name, "root");
            strcpy(out->link_name, "");
            out->last_modified = 0;

            // 根据设备实际能力设置权限
            bool can_read  = (item->entry[i].opr->read  != nullptr);
            bool can_write = (item->entry[i].opr->write != nullptr);
            out->mode = 0;
            if (can_read)  out->mode |= 0444;  // r--r--r--
            if (can_write) out->mode |= 0222;  // -w--w--w-
            return 0;
        }
    }

    return -1;
}

static int opendir(mounting_point*, const char*) {
    return 0;
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    if (inode_id != 0) return 0;
    if (!mp->data || (reinterpret_cast<dev_item*>(mp->data)->devcnt <= inode_id)) return 0;
    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    if (offset >= item->devcnt || !item->entry[offset].opr) return 0;
    out->inode = item->entry[offset].dev_id;
    strcpy(out->name, item->entry[offset].name);
    out->type = '?';
    return 1;
}

static int closedir(mounting_point*, uint32_t) {
    return 0;
}

void init_devfs() {
    dev_fs_operation.mount = &mount;
    dev_fs_operation.unmount = &unmount;
    dev_fs_operation.open = &open;
    dev_fs_operation.read = &read;
    dev_fs_operation.write = &write;
    dev_fs_operation.close = &close;
    dev_fs_operation.opendir = &opendir;
    dev_fs_operation.readdir = &readdir;
    dev_fs_operation.closedir = &closedir;
    dev_fs_operation.stat = &stat;
    register_fs_operation(FS_DRIVER::DEVFS, &dev_fs_operation);
}

int register_in_devfs(mounting_point* mp, const char* dev_name, dev_operation* opr) {
    if (!mp->data || (reinterpret_cast<dev_item*>(mp->data)->devcnt >= MAX_DEV_NUM)) return -1;
    dev_item* item = reinterpret_cast<dev_item*>(mp->data);
    item->entry[item->devcnt].dev_id = item->devcnt;
    strcpy(item->entry[item->devcnt].name, dev_name);
    item->entry[item->devcnt].opr = opr;
    return item->devcnt++;
}
