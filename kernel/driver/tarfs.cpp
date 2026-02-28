#include <driver/vfs.h>
#include <driver/tarfs.h>

#include <kernel/mm.h>
#include <string.h>

fs_operation tar_fs_operation;

int mount(mounting_point* mp) {
    tarfs_data* tdata = (tarfs_data*)kmalloc(sizeof(tarfs_data));
    if (!mp->data) return -1;
    tarfs_metadata* mdata = reinterpret_cast<tarfs_metadata*>(mp->data);
    tdata->tar_addr = mdata->data;
    tdata->tar_size = mdata->size;
    memset(tdata->file_handles, 0, sizeof(tdata->file_handles));
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
