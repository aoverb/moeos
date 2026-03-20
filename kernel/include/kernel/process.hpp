#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdint.h>
#include <pipe.h>
#include <kernel/spinlock.hpp>
#ifdef __cplusplus
extern "C" {
#endif
typedef int pid_t;

constexpr pid_t MAX_PROCESSES_NUM = 65536;
constexpr uint32_t KERNEL_STACK_SIZE = 4096;
constexpr uint32_t MAX_FD_NUM = 128;
constexpr uint32_t USER_STACK_PAGE_SIZE = 64;
struct mounting_point;

constexpr const char* KERNEL_PROC_NAME_IDLE = "idle";
constexpr const char* KERNEL_PROC_NAME_KTIMER = "ktimerd";
constexpr const char* KERNEL_PROC_NAME_KNET = "knetd";

enum class process_state {
    READY = 0,
    RUNNING = 1,
    SLEEPING = 2,
    ZOMBIE = 3,
    WAITING = 4
};

struct PCB;
typedef PCB* process_queue;

typedef struct {
    mounting_point* mp;
    uint32_t inode_id;
    uint32_t offset;
    uint32_t mode;
    uint32_t handle_id;
    uint32_t refcnt;
    char path[255];
    process_queue* poll_queue;
} file_description;

typedef struct PCB {
    pid_t pid;
    uintptr_t esp;
    uintptr_t cr3;
    uint32_t saved_eflags;
    // 该任务的内核栈底（用于释放内存）
    void* kernel_stack_bottom;
    uintptr_t heap_start;  // 堆起始地址（固定不变）
    uintptr_t heap_break;  // 当前堆顶（sbrk 移动这个）

    char name[64];
    uint16_t priority;
    uint16_t quota;
    uint32_t create_time;

    PCB* prev = nullptr;
    PCB* next = nullptr;

    pid_t parent_pid;
    uint8_t to_exit;
    int exit_code;
    process_state state;
    process_queue waiting_queue;
    uint32_t signal;
    process_queue* inwait_queue;

    spinlock plock;
    file_description* fd[MAX_FD_NUM];
    uint32_t fd_num;

    uint32_t proc_node_id;

    char cwd[256];
} PCB;

extern PCB* process_list[MAX_PROCESSES_NUM];
extern pid_t cur_process_id;
extern spinlock process_list_lock;

bool insert_into_process_queue(process_queue& queue, PCB* process);
void remove_from_process_queue(process_queue& queue, pid_t pid);
bool insert_into_waiting_queue(process_queue& queue, PCB* process);
void remove_from_waiting_queue(process_queue& queue, pid_t pid);

void process_init();

pid_t create_process(const char* name, void* entry, void* args);
uint32_t exit_process(pid_t pid, int exit_code);
pid_t exec(const char* name, void* code, uint32_t code_size, uint8_t priority, int argc, char** argv,
    fd_remap* remaps, int remap_cnt);

int waitpid(pid_t child);

void process_switch_to(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif
