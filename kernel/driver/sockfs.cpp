#include <driver/vfs.hpp>
#include <driver/sockfs.hpp>
#include <kernel/schedule.hpp>
#include <kernel/timer.hpp>
#include <kernel/mm.hpp>
#include <kernel/panic.h>

#include <kernel/net/socket.hpp>
#include <kernel/net/icmp.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/net/ip.hpp>

#include <string.h>
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
    spinlock socket_lock;
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
        new_node->next = cur_sock.data.icmp.queue_head;
        cur_sock.data.icmp.queue_head = new_node;
    }
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = cur_sock.wait_queue) {
            remove_from_waiting_queue(cur_sock.wait_queue, cur->pid);
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
    reinterpret_cast<socketfs_data*>(mp->data)->sock[0].valid = 1;
    return 0;
}

static int unmount(mounting_point*) {
    return -1;
}

uint32_t init_new_socket(socketfs_data* data) {
    SpinlockGuard guard(data->socket_lock);
    uint32_t new_sock_num = 0;
    for (int i = 0; i < MAX_SOCK_NUM; ++i) {
        if (data->sock[i].valid == 0) {
            new_sock_num = i;
            data->sock[i].valid = 1;
            break;
        }
    }
    return new_sock_num;
}

static int open(mounting_point* mp, const char* protocol,  uint8_t mode) {
    ++protocol; // 默认第一位是斜线，todo，要校验输入
    if (!strlen(protocol)) {
        return -1;
    }
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;

    if (mode == O_CREATE) { // 创建一个套接字
        uint32_t new_sock_num = init_new_socket(data);
        socket& new_sock = data->sock[new_sock_num];
        new_sock.inode_id = new_sock_num;
        if (new_sock_num == 0) { // 套接字数量已到达最大值
            return -1;
        }
        int ret = 0;
        if (strcmp("icmp", protocol) == 0) {
            ret = icmp_init(new_sock);
        } else if (strcmp("tcp", protocol) == 0) {
            ret = tcp_init(new_sock, 60001);  // TODO：应该是要有一个全局端口池分配的
        } else if (strcmp("udp", protocol) == 0) {
            ret = udp_init(new_sock, 60001);  // TODO：应该是要有一个全局端口池分配的
        }else {
            return -1; // 不支持的协议
        }
        if (ret != 0) {
            return ret;
        }
        return new_sock_num;
    } else { // 打开已有的套接字
        return -1; // todo: 暂不支持
    }
}

static int close(mounting_point* mp, uint32_t inode_id, uint32_t) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    if (cur_sock.ptcl == protocol::ICMP) {
        return icmp_close(cur_sock);
    } else if (cur_sock.ptcl == protocol::TCP) {
        return tcp_close(cur_sock);
    } else if (cur_sock.ptcl == protocol::UDP) {
        return udp_close(cur_sock);
    }
    return 0;
}

static int read(mounting_point* mp, uint32_t inode_id, uint32_t, char* buffer, uint32_t size) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    if (cur_sock.ptcl == protocol::ICMP) {
        return icmp_read(cur_sock, buffer, size);
    } else if (cur_sock.ptcl == protocol::TCP) {
        return tcp_read(cur_sock, buffer, size);
    } else if (cur_sock.ptcl == protocol::UDP) {
        return udp_read(cur_sock, buffer, size);
    }
    return -1;
}

static int write(mounting_point* mp, uint32_t inode_id, uint32_t offset, const char* buffer, uint32_t size) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    char* id_modified_buffer = (char*)kmalloc(size);
    memcpy(id_modified_buffer, buffer, size);
    int ret = -1;
    if (cur_sock.ptcl == protocol::ICMP) {
        ret = icmp_write(cur_sock, id_modified_buffer, size);
    } else if (cur_sock.ptcl == protocol::TCP) {
        ret = tcp_write(cur_sock, id_modified_buffer, size);
    } else if (cur_sock.ptcl == protocol::UDP) {
        ret = udp_write(cur_sock, id_modified_buffer, size);
    }
    kfree(id_modified_buffer);
    return ret;
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

static int ioctl(mounting_point* mp, uint32_t inode_id, uint32_t request, void* arg) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::TCP) {
        return tcp_ioctl(sock.data.tcp.block, request, arg);
    } else if (sock.ptcl == protocol::UDP) {
        return udp_ioctl(sock, request, arg);
    }
    return -1;
}

static int connect(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    int tmp[4];
    sscanf_s(addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t dst_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };
    uint32_t trans_addr = ipv4addr(dst_addr).addr;
    if (sock.ptcl == protocol::ICMP) {
        return icmp_connect(sock, trans_addr, port);
    } else if (sock.ptcl == protocol::TCP) {
        return tcp_connect(sock, trans_addr, port);
    } else if (sock.ptcl == protocol::UDP) {
        return udp_connect(sock, trans_addr, port);
    }
    return -1;
}

int listen(mounting_point* mp, uint32_t inode_id, size_t queue_length) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::TCP) {
        return tcp_listen(sock, queue_length);
    }
    return -1;
}

int accept(mounting_point* mp, uint32_t inode_id, sockaddr* peeraddr, size_t* size) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::TCP) {
        // 准备好包装的inode
        uint32_t new_sock_num = init_new_socket(data);
        socket& new_sock = data->sock[new_sock_num];
        if (new_sock_num == 0) { // 套接字数量已到达最大值
            return -1;
        }
        new_sock.data.tcp.block = tcp_accept(sock, peeraddr, size);
        if (new_sock.data.tcp.block == nullptr) {
            new_sock.valid = 0;
            return -1;
        }
        new_sock.inode_id = new_sock_num;
        new_sock.data.tcp.block->owner = &new_sock;
        new_sock.ptcl = protocol::TCP;
        return new_sock_num;
    }
    return -1;
}

int sendto(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size, sockaddr* peeraddr) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::UDP) {
        return udp_sendto(sock, buffer, size, peeraddr);
    }
    return -1;
}

int recvfrom(mounting_point* mp, uint32_t inode_id, char* buffer, uint32_t size, sockaddr* peeraddr) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::UDP) {
        return udp_recvfrom(sock, buffer, size, peeraddr);
    }
    return -1;
}

static int peek(mounting_point* mp, uint32_t inode_id) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::TCP) {
        if (sock.data.tcp.block == nullptr) return -1;
        if (!(sock.data.tcp.block == nullptr) && sock.data.tcp.block->state != tcb_state::ESTABLISHED) return -2;
        return sock.data.tcp.block->window_used_size;
    } else if (sock.ptcl == protocol::UDP) {
        return sock.data.udp.pack_head != nullptr;
    }
}

static int set_poll(mounting_point* mp, uint32_t inode_id, process_queue* poll_queue) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    sock.poll_queue = poll_queue;
    return 0;
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
    sock_fs_operation.set_poll = &set_poll;
    sock_fs_operation.peek = &peek;
    sock_fs_operation.sock_opr = &sock_operations;
    sock_operations.connect = &connect;
    sock_operations.listen = &listen;
    sock_operations.accept = &accept;
    sock_operations.sendto = &sendto;
    sock_operations.recvfrom = &recvfrom;
    register_fs_operation(FS_DRIVER::SOCKFS, &sock_fs_operation);
}
