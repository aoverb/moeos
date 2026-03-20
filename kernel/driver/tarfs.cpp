#include <driver/vfs.hpp>
#include <driver/tarfs.hpp>

#include <kernel/mm.hpp>
#include <string.h>
extern "C" int printf(const char* fmt, ...);
fs_operation tar_fs_operation;

constexpr uint32_t READ = 1;
constexpr uint32_t MAX_INODE_NUM = 32768;

typedef struct {
    char name[100];
    char filemode[8];
    char owner_id[8];
    char group_id[8];
    char size[12];
    char last_modified[12];
    char checksum[8];
    char type;
    char name_of_link[100];
    char ustar_indicator[6];
    char ustar_ver[2];
    char owner_name[32];
    char group_name[32];
    char dev_major_no[8];
    char dev_minor_no[8];
    char name_prefix[155];
    char padding[12];
} tar_block;
static_assert(sizeof(tar_block) == 512, "tar_block must be 512 bytes!");

struct tar_inode {
    tar_block* block = nullptr;
    std::unordered_map<std::string, inode_id> child_inodes;
};

struct tarfs_data {
    void* tar_addr;
    uint32_t tar_size;
    tar_inode* inodes[MAX_INODE_NUM]; 
    uint32_t inode_cnt;
    spinlock lock;
};

constexpr char TYPE_DIR = '5';

void make_root_block(tar_block* root) {
    strcpy(root->name, "/");
    root->type = TYPE_DIR;
}

uint32_t checksum(tar_block* block) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(block);
    uint32_t sum = 0;

    for (int i = 0; i < 512; i++) {
        // checksum 字段位于偏移 148~155，计算时当作空格
        if (i >= 148 && i < 156)
            sum += 0x20;
        else
            sum += bytes[i];
    }

    return sum;
}

uint32_t parse_octal(const char* s, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '7') break;
        val = val * 8 + (s[i] - '0');
    }
    return val;
}

tar_inode* construct_inode_by_path(tarfs_data* tdata, const char* path) {
    tar_inode* cur_node = tdata->inodes[0];
    char fix_s[256];
    char* s = fix_s;

    uint32_t i = 0;
    uint32_t prev_inode_id = 0;
    while (i < 155 && path[i] != '\0') {
        if (path[i] == '/') { ++i; continue; }

        uint32_t s_len = 0;
        while(i < 155 && path[i] != '/' && path[i] != '\0' && s_len < 255) {
            s[s_len] = path[i];
            ++i;
            ++s_len;
        }
        s[s_len] = '\0';
        if (s[0] == '\0') break;
        if (cur_node->child_inodes.find(s) == cur_node->child_inodes.end()) {
            if (tdata->inode_cnt > MAX_INODE_NUM) {
                return nullptr;
            }
            uint32_t new_id = tdata->inode_cnt++;
            cur_node->child_inodes[s] = new_id;

            tar_inode* new_node = new (kmalloc(sizeof(tar_inode))) tar_inode();
            new_node->block = nullptr;
            new_node->child_inodes.clear();
            new_node->child_inodes["."] = new_id;
            new_node->child_inodes[".."] = prev_inode_id;
            tdata->inodes[new_id] = new_node;
        }
        prev_inode_id = cur_node->child_inodes[s];
        cur_node = tdata->inodes[cur_node->child_inodes[s]];
    }
    return cur_node;
}

tar_inode* get_inode_by_path(tarfs_data* tdata, const char* path) {
    tar_inode* cur_node = tdata->inodes[0];
    char fix_s[256];
    char* s = fix_s;

    uint32_t i = 0;
    while (i < 155 && path[i] != '\0') {
        if (path[i] == '/') { ++i; continue; }

        uint32_t s_len = 0;
        while(i < 155 && path[i] != '/' && path[i] != '\0' && s_len < 255) {
            s[s_len] = path[i];
            ++i;
            ++s_len;
        }
        s[s_len] = '\0';
        if (s[0] == '\0') break;

        if (cur_node->child_inodes.find(s) == cur_node->child_inodes.end()) {
            return nullptr;
        }
        cur_node = tdata->inodes[cur_node->child_inodes[s]];
    }
    return cur_node;
}

int construct_index(tarfs_data* tdata) {
    // 构造根目录块
    tdata->inodes[0] = new (kmalloc(sizeof(tar_inode))) tar_inode();
    tar_inode* root = tdata->inodes[0];
    root->block = reinterpret_cast<tar_block*>(kmalloc(sizeof(tar_block)));
    root->child_inodes["."] = 0;
    root->child_inodes[".."] = 0;
    make_root_block(root->block);
    tdata->inode_cnt++;

    void* cur_addr = tdata->tar_addr;
    tar_block* cur_block;

    while ((uintptr_t)cur_addr - (uintptr_t)tdata->tar_addr < tdata->tar_size) {
        cur_block = reinterpret_cast<tar_block*>(cur_addr);

        if (cur_block->name[0] == '\0' && cur_block->name_prefix[0] == '\0') {
            break;
        }

        if (checksum(cur_block) != parse_octal(cur_block->checksum, 8)) {
            printf("corrupted block at offset %x\n",
                (uint32_t)((uintptr_t)cur_addr - (uintptr_t)tdata->tar_addr));
             // todo: cleaning
            return -1;
        }
        if (cur_block->name_prefix[0] == '\0') {
            strcpy(cur_block->name_prefix, cur_block->name);
            int len = strlen(cur_block->name);
            if (cur_block->name[len - 1] == '/') --len;
            while(--len >= 0 && cur_block->name_prefix[len] != '/') cur_block->name_prefix[len] = '\0';
            if (len >= 0) cur_block->name_prefix[len] = '\0';
            int idx = 0;
            int len_prefix = strlen(cur_block->name_prefix);
            while (cur_block->name[idx] != '\0') {
                cur_block->name[idx] = cur_block->name[idx + len_prefix + 1];
                ++idx;
            }
            cur_block->name[idx] = '\0';
        }
        tar_inode* dir = construct_inode_by_path(tdata, cur_block->name_prefix);
        
        if (dir == nullptr) {
            // todo: cleaning
            return -1;
        }
        char key[128];
        strcpy(key, cur_block->name);
        // 如果最后一位是斜杠需要去掉
        if (strlen(key) && key[strlen(key) - 1] == '/') {
            key[strlen(key) - 1] = '\0';
        }
        if (*key) {
            if (dir->child_inodes.find(key) == dir->child_inodes.end()) {
                tar_inode* new_node = new (kmalloc(sizeof(tar_inode))) tar_inode();
                dir->child_inodes[key] = tdata->inode_cnt;
                tdata->inodes[tdata->inode_cnt++] = new_node;
                if (tdata->inode_cnt > MAX_INODE_NUM) {
                    // todo: cleaning
                    return -1;
                }
            }
            tdata->inodes[dir->child_inodes[key]]->block = cur_block;
            tdata->inodes[dir->child_inodes[key]]->child_inodes["."] = dir->child_inodes[key];
            tdata->inodes[dir->child_inodes[key]]->child_inodes[".."] = dir->child_inodes["."];
        }

        uint64_t file_size = parse_octal(cur_block->size, 12);
        cur_addr = (uint8_t*)cur_addr + 512 + ((file_size + 511) / 512) * 512;
    }
    return 0;
}

static int mount(mounting_point* mp) {
    tarfs_data* tdata = (tarfs_data*)kmalloc(sizeof(tarfs_data));
    if (!mp->data) return -1;
    tarfs_metadata* mdata = reinterpret_cast<tarfs_metadata*>(mp->data);
    tdata->tar_addr = mdata->data;
    tdata->tar_size = mdata->size;
    tdata->inode_cnt = 0;
    tdata->lock.locked = 0;
    memset(tdata->inodes, 0, sizeof(tdata->inodes));
    if (construct_index(tdata) != 0) {
        // todo: cleaning
        return -1;
    }
    mp->data = tdata;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path, uint8_t) {
    tarfs_data* data = reinterpret_cast<tarfs_data*>(mp->data);
    SpinlockGuard guard(data->lock);
    tar_inode* file_inode = get_inode_by_path(data, path);
    if (file_inode == nullptr) return -1;
    return file_inode->child_inodes["."];
}

static int close(mounting_point*, uint32_t, uint32_t) {
    return 0;
}

static void print_field(const char* label, const char* field, int max_len) {
    char buf[256];
    memcpy(buf, field, max_len);
    buf[max_len] = '\0';
    printf("%s %s\n", label, buf);
}

static void tar_dump_block(tar_block* block) {
    if (!block) {
        printf("tar_block: NULL\n");
        return;
    }

    printf("=== tar_block ===\n");
    print_field("name:         ", block->name,           100);
    print_field("filemode:     ", block->filemode,         8);
    print_field("owner_id:     ", block->owner_id,         8);
    print_field("group_id:     ", block->group_id,         8);
    print_field("size:         ", block->size,            12);
    print_field("last_modified:", block->last_modified,   12);
    print_field("checksum:     ", block->checksum,         8);
    printf(     "type:          %c (%d)\n", block->type, block->type);
    print_field("name_of_link: ", block->name_of_link,   100);
    print_field("ustar:        ", block->ustar_indicator,  6);
    print_field("ustar_ver:    ", block->ustar_ver,        2);
    print_field("owner_name:   ", block->owner_name,      32);
    print_field("group_name:   ", block->group_name,      32);
    print_field("dev_major_no: ", block->dev_major_no,     8);
    print_field("dev_minor_no: ", block->dev_minor_no,     8);
    print_field("name_prefix:  ", block->name_prefix,    155);
    printf("=================\n");
}

static int tar_read(tarfs_data* data, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    tar_inode* file_inode = data->inodes[inode_id];
    // tar_dump_block(file_inode->block);
    uint32_t file_size = parse_octal(file_inode->block->size, 12);
    if (offset >= file_size) {
        return 0;  // EOF
    }
    if (offset + size > file_size) {
        size = file_size - offset;
    }
    memcpy(buffer, (char*)file_inode->block + 512 + offset, size);
    return size;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    tarfs_data* data = reinterpret_cast<tarfs_data*>(mp->data);
    SpinlockGuard guard(data->lock);
    if (inode_id >= data->inode_cnt || !data->inodes[inode_id]) {
        return -1;
    }
    return tar_read(data, inode_id, offset, buffer, size);
}

static int write(mounting_point*, uint32_t, uint32_t, const char*, uint32_t) {
    // read-only filesystem
    return -1;
}

static int opendir(mounting_point* mp, const char* path) {
    return open(mp, path, 1);
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    tarfs_data* data = reinterpret_cast<tarfs_data*>(mp->data);
    SpinlockGuard guard(data->lock);
    if (inode_id >= data->inode_cnt) return -1;
    tar_inode* dir_inode = data->inodes[inode_id];
    auto& children = dir_inode->child_inodes;

    if (offset >= children.size()) {
        return 0;
    }

    auto it = children.begin();
    for (uint32_t i = 0; i < offset; ++i) {
        ++it; // 非常拉跨
    }

    strncpy(out->name, it->first.c_str(), 255);
    
    out->name[255] = '\0';
    out->inode = it->second;

    tar_inode* child = data->inodes[it->second];
    if (child && child->block) {
        out->type = child->block->type;
    } else {
        out->type = TYPE_DIR;
    }
    return 1;
}

static int peek(mounting_point* mp, uint32_t inode_id) {
    return 0; // todo: 暂不支持
}

static int closedir(mounting_point* mp, uint32_t inode_id) {
    return close(mp, inode_id, 0);
}

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    tarfs_data* data = reinterpret_cast<tarfs_data*>(mp->data);
    SpinlockGuard guard(data->lock);
    tar_inode* inode = get_inode_by_path(data, path);
    if (inode == nullptr || inode->block == nullptr) return -1;

    tar_block* blk = inode->block;

    out->size          = parse_octal(blk->size, 12);
    out->type          = blk->type == TYPE_DIR ? 0 : 1;
    out->mode          = parse_octal(blk->filemode, 8);
    out->owner_id      = parse_octal(blk->owner_id, 8);
    out->group_id      = parse_octal(blk->group_id, 8);
    out->last_modified = parse_octal(blk->last_modified, 12);

    strncpy(out->owner_name, blk->owner_name, 32);
    strncpy(out->group_name, blk->group_name, 32);
    strncpy(out->name,       blk->name, 100);
    strncpy(out->link_name,  blk->name_of_link, 100);

    return 0;
}

void init_tarfs() {
    tar_fs_operation.mount = &mount;
    tar_fs_operation.unmount = &unmount;
    tar_fs_operation.open = &open;
    tar_fs_operation.read = &read;
    tar_fs_operation.write = &write;
    tar_fs_operation.close = &close;
    tar_fs_operation.opendir = &opendir;
    tar_fs_operation.readdir = &readdir;
    tar_fs_operation.closedir = &closedir;
    tar_fs_operation.stat = &stat;
    tar_fs_operation.peek = &peek;
    tar_fs_operation.set_poll = nullptr;
    tar_fs_operation.ioctl = nullptr;
    tar_fs_operation.truncate = nullptr;
    tar_fs_operation.sock_opr = nullptr;
    register_fs_operation(FS_DRIVER::TARFS, &tar_fs_operation);
}
