#include <driver/vfs.hpp>
#include <driver/sockfs.hpp>
#include <kernel/schedule.hpp>
#include <kernel/timer.hpp>
#include <kernel/mm.hpp>
#include <kernel/panic.h>

#include <kernel/net/socket.hpp>
#include <kernel/net/icmp.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/net/ip.hpp>

#include <string.h>
#include <stdio.h>
#include <format.h>

fs_operation sock_fs_operation;
sock_operation sock_operations;

static mounting_point* global_mp;

typedef struct {
    pid_t pid;
    bool  expired;
    bool  cancelled;
} icmp_timeout_ctx;

typedef struct {
    socket sock[MAX_SOCK_NUM];
} socketfs_data;

void sockfs_icmp_add(int inode_id, char* buffer, size_t size) {
    if (!global_mp || inode_id < 0 || inode_id >= MAX_SOCK_NUM) return;
    socketfs_data* data = (socketfs_data*)global_mp->data;
    socket& cur_sock = data->sock[inode_id];
    {
        SpinlockGuard guard(cur_sock.lock);
        if (data->sock[inode_id].valid == 0 || data->sock[inode_id].ptcl != protocol::ICMP) return;
        icmp_node* new_node = (icmp_node*)kmalloc(sizeof(icmp_node));
        new_node->data = (char*)kmalloc(size);
        memcpy(new_node->data, buffer, size);
        new_node->size = size;
        new_node->next = (icmp_node*)(cur_sock.data);
        cur_sock.data = new_node;
    }
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = cur_sock.wait_queue) {
            remove_from_process_queue(cur_sock.wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
    return;
}

static int mount(mounting_point* mp) {
    if (!mp) return -1;
    static uint8_t mounted = 0;
    if (mounted) {
        panic("SockFS can only be mounted once!"); // 各个网络协议的回调函数限定了这套系统只能挂一套SockFS
    }
    global_mp = mp;
    mp->data = (socketfs_data*)kmalloc(sizeof(socketfs_data));
    memset(mp->data, 0, sizeof(socketfs_data));
    reinterpret_cast<socketfs_data*>(mp->data)->sock[0].ptcl = protocol::ROOT;
    strcpy(reinterpret_cast<socketfs_data*>(mp->data)->sock[0].dst_addr, ".");
    reinterpret_cast<socketfs_data*>(mp->data)->sock[0].valid = 1;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

static int open(mounting_point* mp, const char* path,  uint8_t mode) {
    ++path; // 第一位是斜线
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (mode == O_CREATE) { // 创建一个套接字
        uint32_t new_sock_num = 0;
        for (int i = 0; i < MAX_SOCK_NUM; ++i) {
            if (data->sock[i].valid == 0) {
                new_sock_num = i;
                break;
            }
        }
        if (new_sock_num == 0) { // 套接字数量已到达最大值
            return -1;
        }
        socket& new_sock = data->sock[new_sock_num];
        new_sock.valid = 1;

        uint8_t src_addr[4];
        getLocalNetconf()->ip.to_bytes(src_addr);
        sprintf(new_sock.src_addr, "%d.%d.%d.%d", src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
        new_sock.src_port = 60001; // TODO：应该是要有一个全局端口池分配的

        // 创建socket格式：<addr>/<protocol>
        char protocol[8];
        strcpy(protocol, path);
        strcpy("127.0.0.1", new_sock.dst_addr); // 先给一个默认地址，后面通过connect设置

        if (!strlen(protocol)) {
            return -1;
        }

        if (strcmp("icmp", protocol) == 0) {
            new_sock.ptcl = protocol::ICMP;
        } else if (strcmp("tcp", protocol) == 0) {
            new_sock.ptcl = protocol::TCP;
        } else {
            // 不支持的协议
            return -1;
        }
        return new_sock_num;
    } else { // 打开已有的套接字
        return -1; // todo: 暂不支持
    }
    return -1;
}

static int close(mounting_point* mp, uint32_t inode_id, uint32_t) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    if (cur_sock.ptcl == protocol::ICMP) {
        SpinlockGuard guard(cur_sock.lock);
        PCB* cur;
        while(cur = cur_sock.wait_queue) {
            remove_from_process_queue(cur_sock.wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
        icmp_node* node = (icmp_node*)cur_sock.data;
        while (node) {
            icmp_node* next = node->next;
            kfree(node->data);
            kfree(node);
            node = next;
        }
        cur_sock.valid = 0;
    }
    return 0;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    if (cur_sock.ptcl == protocol::ICMP) {
        int ret = -1;
        {
            uint32_t flags = spinlock_acquire(&(cur_sock.lock));
            if (cur_sock.data) {
                icmp_node* icmp_data = (icmp_node*)cur_sock.data;
                size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                memcpy(buffer, icmp_data->data, cpysize);
                icmp_node* next = icmp_data->next;
                kfree(icmp_data->data);
                kfree(cur_sock.data);
                cur_sock.data = next;
                ret = cpysize;
            } else {
                {
                    SpinlockGuard guard(process_list_lock);
                    process_list[cur_process_id]->state = process_state::WAITING;
                    insert_into_process_queue(cur_sock.wait_queue, process_list[cur_process_id]);
                }
                spinlock_release(&(cur_sock.lock), flags);
                timeout(&(cur_sock.wait_queue), 1000);
                uint32_t flags = spinlock_acquire(&(cur_sock.lock));
                if (cur_sock.data) {
                    icmp_node* icmp_data = (icmp_node*)cur_sock.data;
                    size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                    memcpy(buffer, icmp_data->data, cpysize);
                    icmp_node* next = icmp_data->next;
                    kfree(icmp_data->data);
                    kfree(cur_sock.data);
                    cur_sock.data = next;
                    ret = cpysize;
                }
            }
            spinlock_release(&(cur_sock.lock), flags);
        }

        return ret;
    }
    return -1;
}

static int write(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    if (cur_sock.ptcl == protocol::ICMP) {
        uint8_t trans_addr[4];
        unsigned int a, b, c, d;
        sscanf_s(cur_sock.dst_addr, "%u.%u.%u.%u", &a, &b, &c, &d);
        trans_addr[0] = (uint8_t)a;
        trans_addr[1] = (uint8_t)b;
        trans_addr[2] = (uint8_t)c;
        trans_addr[3] = (uint8_t)d;
        ipv4addr addr = ipv4addr(trans_addr);

        char* id_modified_buffer = (char*)kmalloc(size);
        memcpy(id_modified_buffer, buffer, size);
        // 虽然我们现在的socket数量最大是1024，在这个情况下这个标识符应该是但是我不排除后面会有调大这个数量的情况
        // 我不希望调大socket数的时候忘掉了修改这里导致标识符冲突，因此我需要在这里加个断言...
        // 虽然现在这个断言永不失败，但谁知道呢？或许后面我会把MAX_SOCK_NUM换个类型。
        static_assert(MAX_SOCK_NUM <= 65536);
        *reinterpret_cast<uint16_t*>(id_modified_buffer + 4) = (uint16_t)inode_id;
        *reinterpret_cast<uint16_t*>(id_modified_buffer + 2) = 0;
        uint16_t chksum = checksum(id_modified_buffer, size);
        *reinterpret_cast<uint16_t*>(id_modified_buffer + 2) = chksum;
        int ret = send_ipv4(addr, IP_PROTOCOL_ICMP, id_modified_buffer, size);
        kfree(id_modified_buffer);
        return ret;
    }

    return -1;
}

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    if (!mp->data) return -1;
    if (path[0] == '/') path++;

    if (path[0] == '\0') {
        strcpy(out->name, "sock");
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
/* 这块最好是配合IPC做，怎么做暂时没想好
    socketfs_data* data = reinterpret_cast<socketfs_data*>(mp->data);
    for (uint32_t i = 0; i < data->sock_num; ++i) {
        if (data->entry[i].opr && strcmp(path, data->entry[i].name) == 0) {
            strcpy(out->name, item->entry[i].name);
            out->type = 1;
            out->size = 0;
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
*/
    return -1;
}

static int opendir(mounting_point*, const char*) {
    return 0;
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    // 这块最好也是配合IPC做
    return 0;
}

static int closedir(mounting_point*, uint32_t) {
    return 0;
}

static int ioctl(mounting_point*, uint32_t, const char* cmd, void* arg) {
    return 0;
}

static int connect(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::ICMP) {
        return icmp_connect(sock, addr, port);
    } else if (sock.ptcl == protocol::TCP) {
        return tcp_connect(sock, addr, port);
    }
}

void init_sockfs() {
    sock_fs_operation.mount = &mount;
    sock_fs_operation.unmount = &unmount;
    sock_fs_operation.open = &open;
    sock_fs_operation.read = &read;
    sock_fs_operation.write = &write;
    sock_fs_operation.close = &close;
    sock_fs_operation.opendir = &opendir;
    sock_fs_operation.readdir = &readdir;
    sock_fs_operation.closedir = &closedir;
    sock_fs_operation.stat = &stat;
    sock_fs_operation.ioctl = &ioctl;
    sock_fs_operation.sock_opr = &sock_operations;
    sock_operations.connect = &connect;

    register_fs_operation(FS_DRIVER::SOCKFS, &sock_fs_operation);
}
