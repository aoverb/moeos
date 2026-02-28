#include <driver/vfs.h>
#include <driver/tarfs.h>

#include <kernel/mm.h>
#include <string.h>
#include <stdio.h>

fs_operation tar_fs_operation;

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
    char s[256];

    uint32_t i = 0;
    uint32_t prev_inode_id = MAX_INODE_NUM;
    while (i < 155 && path[i] != '\0') {
        uint32_t s_len = 0;
        while(i < 155 && path[i] != '/' && path[i] != '\0' && s_len < 255) {
            s[s_len] = path[i];
            ++i;
            ++s_len;
        }
        s[s_len] = '\0';
        if (s[0] == '\0') break;

        if (cur_node->child_inodes.find(s) == cur_node->child_inodes.end()) {
            cur_node = new (kmalloc(sizeof(tar_inode))) tar_inode();
            // 当前节点没有被真正从tar_block读取进去，为了避免出现错误，需要简单初始化
            cur_node->block = nullptr;
            cur_node->child_inodes.clear();
            cur_node->child_inodes[s] = tdata->inode_cnt;
            cur_node->child_inodes["."] = tdata->inode_cnt;
            cur_node->child_inodes[".."] = prev_inode_id;
            tdata->inodes[tdata->inode_cnt++] = cur_node;
        }
        prev_inode_id = cur_node->child_inodes[s];
        cur_node = tdata->inodes[cur_node->child_inodes[s]];
    }
    return cur_node;
}

tar_inode* construct_index(tarfs_data* tdata) {
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

    while ((uint32_t)cur_addr - (uint32_t)tdata->tar_addr < tdata->tar_size) {
        cur_block = reinterpret_cast<tar_block*>(cur_addr);
        if (checksum(cur_block) != parse_octal(cur_block->checksum, 8)) {
            // corrupted
            // todo: cleaning
            return nullptr;
        }
        tar_inode* dir = construct_inode_by_path(tdata, cur_block->name_prefix);
        if (dir->child_inodes.find(cur_block->name) == dir->child_inodes.end()) {
            tar_inode* new_node = new (kmalloc(sizeof(tar_inode))) tar_inode();
            dir->child_inodes[cur_block->name] = tdata->inode_cnt;
            tdata->inodes[tdata->inode_cnt++] = new_node;
        }
        tdata->inodes[dir->child_inodes[cur_block->name]]->block = cur_block;

        if (cur_block->type == TYPE_DIR) {
            tdata->inodes[dir->child_inodes[cur_block->name]]->child_inodes["."] = dir->child_inodes[cur_block->name];
            tdata->inodes[dir->child_inodes[cur_block->name]]->child_inodes[".."] = dir->child_inodes["."];
        }

        printf("inode[%d]->%s/%s\n", dir->child_inodes[cur_block->name], cur_block->name_prefix,
            cur_block->name);

        uint64_t file_size = parse_octal(cur_block->size, 12);
        cur_addr = (uint8_t*)cur_addr + 512 + ((file_size + 511) / 512) * 512;
    }

    return root;
}

int mount(mounting_point* mp) {
    tarfs_data* tdata = (tarfs_data*)kmalloc(sizeof(tarfs_data));
    if (!mp->data) return -1;
    tarfs_metadata* mdata = reinterpret_cast<tarfs_metadata*>(mp->data);
    tdata->tar_addr = mdata->data;
    tdata->tar_size = mdata->size;
    tdata->inode_cnt = 0;
    memset(tdata->inodes, 0, sizeof(tdata->inodes));
    memset(tdata->file_handles, 0, sizeof(tdata->file_handles));
    construct_index(tdata);
    mp->data = tdata;
    return 0;
}

int unmount(mounting_point*) {
    return -1;
}

int open(mounting_point* mp, const char* path, uint8_t mode) {
    return -1;
}

int close(mounting_point* mp, uint32_t handle_id) {
    return -1;
}

int tar_read(tarfs_data* data, uint32_t handle_id, char* buffer, uint32_t size) {
    return -1;
}

int read(mounting_point* mp, uint32_t handle_id, char* buffer, uint32_t size) {
    tarfs_data* data = reinterpret_cast<tarfs_data*>(mp->data);
    if (!(data->file_handles[handle_id].mode & READ)) {
        return -1;
    }
    return tar_read(data, handle_id, buffer, size);
}

int write(mounting_point* mp, uint32_t handle_id, const char* buffer, uint32_t size) {
    return -1;
}

int opendir(mounting_point* mp, const char* path) {
    return -1;
}

int readdir(mounting_point* mp, uint32_t handle_id, dirent* out) {
    return -1;
}

int closedir(mounting_point* mp, uint32_t handle_id) {
    return -1;
}

int stat(mounting_point* mp, const char* path, file_stat* out) {
    return -1;
}

void init_tarfs() {
    tar_fs_operation.mount = &mount;
    tar_fs_operation.read = &read;
    register_fs_operation(FS_DRIVER::TARFS, &tar_fs_operation);
}
