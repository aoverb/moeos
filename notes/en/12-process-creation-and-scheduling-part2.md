## Homemade OS (12): Process Creation, Scheduling and Switching (Part 2)

In the previous part, we implemented a preliminary multi-process system in our homemade OS. It's "preliminary" because the current scheduler's algorithm isn't fair, it shouldn't exclude process 0 from scheduling, and our entry point function should accept parameters.

### Separating the Shell

Let's start with something simple: separate the shell from our process 0 and make it process 1. Essentially, we extract it as a function and start it using `create_process` in `kernel_main`.

```cpp
extern "C" void kernel_main(multiboot_info_t* mbi) {
    pmm_prepare(mbi);
    
    ...
    
    printf("Welcome, aoverb!\n\n");
    printf("The kernel_main lies in %X, sounds great!\n\n", &kernel_main);
    
    create_process(reinterpret_cast<void*>(&shell));
    while (1) {
        yield();
    }
}
```

We make process 0 yield continuously.

### A Fairer Scheduling Algorithm — MLFQ

The current scheduling algorithm is too simple. First, it relies on processes voluntarily yielding — if a process never yields, other processes "starve". Second, it favors processes with lower indices, which is completely unjustified.

```cpp
void yield() {
    for (uint8_t i = 1; i < MAX_PROCESSES_NUM; ++i) {
        if (i == cur_process_id) continue;
        if (process_list[i]) {
            process_switch_to(i);
            return;
        }
    }
    // If no other process can be switched to, go back to process 0
    process_switch_to(0);
    return;
}
```

Let's upgrade it to the MLFQ (Multi-Level Feedback Queue) algorithm. This algorithm uses multiple queues, each representing a priority level. It forcibly performs rescheduling at regular intervals, prioritizing processes in the highest-priority queue. All processes start in the highest queue. When their time quota for the current queue is exhausted, they're moved to a lower-priority queue and given a new quota. Finally, after a certain period, all processes are moved back to the highest-priority queue.

This algorithm has many benefits: no starvation, faster response times, etc. For a detailed introduction, check out OSTEP.

#### Data Structure

```cpp
constexpr uint8_t NUM_PRIORITY = 5;
constexpr uint8_t MAX_PRIORITY = NUM_PRIORITY - 1;
constexpr uint8_t MAP_PRIORITY_TO_QUOTA[NUM_PRIORITY] = {32, 16, 8, 4, 2};
PCB* sche_queue_head[NUM_PRIORITY];

constexpr uint16_t RESETCNT_INITIAL = 500;
uint16_t resetcnt = RESETCNT_INITIAL;
```

I'm using 5 priority queues, where higher index = higher priority. The corresponding initial time slices are `{32, 16, 8, 4, 2}` (Claude told me this works best), and the priority reset tick count is 500 (also Claude's suggestion).

The queues are implemented as circular linked lists. This has many benefits, the biggest being that I don't need to manually implement queues or track tail pointers, and merging two linked lists is straightforward.

Similarly, we need to add more scheduling-related fields to the PCB, such as priority, remaining time quota, and prev/next pointers:

```cpp
typedef struct PCB {
    ...

    uint16_t priority;
    uint16_t quota;
    PCB* prev;
    PCB* next;
} PCB;
```

#### Insert and Remove Logic

```cpp
void insert_into_scheduling_queue(uint8_t pid);
void remove_from_scheduling_queue(uint8_t pid);
```

I won't elaborate on the implementation details here, but my design intent is to keep these two interfaces simple and efficient (O(1)!). Thanks to the circular linked list, efficient implementation isn't complicated.

#### Scheduling Logic

```cpp
void schedule() {
    PCB* cur_pcb = process_list[cur_process_id];
    if (--(cur_pcb->quota) == 0) {
        if (cur_pcb->priority > 0) {
            remove_from_scheduling_queue(cur_process_id);
            insert_into_queue(cur_pcb, cur_pcb->priority - 1);
        }
        cur_pcb->quota = MAP_PRIORITY_TO_QUOTA[cur_pcb->priority];
    }
    if (--resetcnt == 0) {
        resetcnt = RESETCNT_INITIAL;
        move_all_to_top_priority();
    }
    PCB* chosen_process = nullptr;
    for (int i = NUM_PRIORITY - 1; i >= 0; --i) {
        if (sche_queue_head[i]) {
            chosen_process = sche_queue_head[i];
            sche_queue_head[i] = sche_queue_head[i]->next;
            break;
        }
    }
    if (!chosen_process) {
        chosen_process = process_list[0];
    }
    process_switch_to(chosen_process->pid);
}
```

We trigger this `schedule` from the timer interrupt. The logic is simple: decrement the current process's time quota; if quota reaches 0, lower its priority (or just reset quota if already at lowest priority). The process selection logic ensures that processes at the same priority level don't starve. Additionally, after a longer period (500 ticks = 5 seconds), all processes are moved back to the highest priority.

When switching processes, we select the highest-priority queue that has processes, and advance the queue head pointer to the next entry before switching.

#### Priority Reset Logic

```cpp
void move_all_to_top_priority() {
    for (int i = 1; i < NUM_PRIORITY; ++i) {
        if (!sche_queue_head[i - 1]) continue;
        if (sche_queue_head[i]) {
            sche_queue_head[i]->prev->next = sche_queue_head[i - 1]->next;
            sche_queue_head[i - 1]->next->prev = sche_queue_head[i]->prev;
            sche_queue_head[i]->prev = sche_queue_head[i - 1];
            sche_queue_head[i - 1]->next = sche_queue_head[i];
        } else {
            sche_queue_head[i] = sche_queue_head[i - 1];
        }
        sche_queue_head[i - 1] = nullptr;
    }
    PCB* head = sche_queue_head[MAX_PRIORITY];
    PCB* tail = head;
    do {
        tail->priority = MAX_PRIORITY;
        tail->quota = MAP_PRIORITY_TO_QUOTA[MAX_PRIORITY];
        tail = tail->next;
    } while (tail != head);
}
```

We merge from lower priority queues upward, then reset all processes' priorities and quotas in the top queue. Resetting here is O(n), which is somewhat inefficient, but considering the frequency of reset operations, it's probably not a critical bottleneck.

#### Some Pitfalls

While debugging the scheduler, I noticed processes weren't alternating execution (and the process IDs looked strange... but that's a different issue). At first I thought the time slices were too small or each process wasn't doing enough work, but that wasn't the problem.

After debugging with GDB, I found that interrupts were getting disabled. Further investigation revealed they were being disabled after `process_switch_to`.

![image-20260224183555620](../assets/自制操作系统（12）：进程创建、调度与切换（下）/image-20260224183555620.png)

This relates back to our `process_switch_to` from the previous chapter... I realized we weren't saving eflags, and we weren't enabling interrupts when creating new processes. We needed to fix these two bugs.

![image-20260224183953717](../assets/自制操作系统（12）：进程创建、调度与切换（下）/image-20260224183953717.png)

After fixing these, our multi-process logic ran smoothly. Yes!

But you might notice the console output was garbled — this was because we hadn't added locking to the terminal output. (After all, we hadn't implemented locks yet!) This needs to be fixed later.

### Creating Processes with Parameters

According to the cdecl calling convention, the return address is placed at the top of the stack, with parameters after it. So we pass an additional pointer to parameters in `create_process`, placing it after the return address when constructing the stack frame:

```cpp
uint32_t create_process(void* entry, void* args) {
    for (auto nid = 0; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            ...
            *((uintptr_t*)(new_process->esp - 4)) = reinterpret_cast<uintptr_t>(args);
            *((uintptr_t*)(new_process->esp - 8)) = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
```

Now we can pass parameters like this:

```c++
    const char* c1 = "test1";
    const char* c2 = "test2";
    const char* c3 = "test3";
    create_process(reinterpret_cast<void*>(&proc1), (void*)c1);
    create_process(reinterpret_cast<void*>(&proc1), (void*)c2);
    create_process(reinterpret_cast<void*>(&proc1), (void*)c3);
    create_process(reinterpret_cast<void*>(&shell), nullptr);
```

When there are no parameters, we simply pass a null pointer.

The receiving function in the process receives the parameter and converts it as appropriate:

```cpp
void proc1(void* args) {
    char* s = reinterpret_cast<char*>(args);
    for (uint32_t i = 0; i < 99999999; i++) {
        if (i % 10000000 == 0) printf("%s %d:%d/99999999\n", s, cur_process_id, i);
    }
}
```

![image-20260224195114022](../assets/自制操作系统（12）：进程创建、调度与切换（下）/image-20260224195114022.png)

Now we can pass parameters when creating processes.

### Process States

Currently, our processes have only two implicit states: running and ready. For better management and scheduling optimization, we need to introduce explicit process states — add a `state` field to the PCB to indicate the current process status. For now, we need four states: Ready, Running, Blocked, and Zombie.

```cpp
enum class process_state {
    READY = 0,   // Process is waiting to be scheduled
    RUNNING = 1, // Process is running
    BLOCKED = 2, // Process is waiting for some event
    ZOMBIE = 3   // Process has terminated, waiting for resource reclamation
};

typedef struct PCB {
    uint8_t pid;
    uintptr_t esp;
    // This task's kernel stack bottom (for freeing memory)
    void* kernel_stack_bottom;

    uint16_t priority;
    uint16_t quota;
    uint32_t create_time;
    process_state state;
    PCB* prev;
    PCB* next;
} PCB;
```

Let's discuss the adaptation for each state:

#### Ready and Running States

When a process is created, it's set to the Ready state and added to the scheduling queue.

When a process is scheduled, the currently running process is set to Ready and re-added to the scheduling sequence, while the new process is removed from the scheduling queue and set to Running.

That means, unlike our previous design where all processes were in the scheduling queue and re-inserted after scheduling, now the queue only contains processes in the Ready state.

##### Abstracting Process Queue Logic

Since we anticipate having various queues in the future, it's better to abstract the queue concept from the scheduling queue into a general process queue.

```cpp

bool insert_into_process_queue(process_queue& queue, PCB* process) {
    if (process == nullptr || process->prev != nullptr || process->next != nullptr) {
        // Ignore duplicate insertions
        return false;
    }
 
    if (queue) {
        process->next = queue;
        process->prev = queue->prev;
        queue->prev->next = process;
        queue->prev = process;
    } else {
        process->next = process;
        process->prev = process;
        queue = process;
    }
    return true;
}

void remove_from_process_queue(process_queue& queue, uint8_t pid) {
    // Won't check whether the PCB for pid is in the given queue; caller should verify
    PCB* cur_pcb = process_list[pid];
    PCB* prev_pcb = cur_pcb->prev;
    PCB* next_pcb = cur_pcb->next;
    if (cur_pcb == prev_pcb) {
        queue = nullptr;
        cur_pcb->prev = cur_pcb->next = nullptr;
        return;
    }
    
    if (prev_pcb) prev_pcb->next = next_pcb;
    if (next_pcb) next_pcb->prev = prev_pcb;

    if (queue == cur_pcb) {
        queue = cur_pcb->next;
    }

    cur_pcb->prev = cur_pcb->next = nullptr;
}
```

We'll have various process queues in the future, so extracting this early is better.

#### Blocked State

A blocked process is waiting for some event (e.g., a mutex lock, sleep completion, etc.).

Following the logic above, a running process is not in any queue. So we can conveniently set the current process's state to Blocked, place it into a specific event's waiting queue, and yield the CPU.

When the specific event occurs (meaning the conditions we were waiting for are now satisfied), our process becomes ready. At that point, the event handler sets us back to Ready and puts us back in the scheduling sequence.

#### Zombie (Terminated-Awaiting-Reclamation) State

Our previous process exit logic was:

```cpp
uint32_t exit_process(uint8_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    remove_from_scheduling_queue(pid);
    PCB*& cur_process = process_list[pid];
    kfree(reinterpret_cast<void*>(cur_process->kernel_stack_bottom)); // Kernel stack is freed!
    kfree(reinterpret_cast<void*>(cur_process));
    cur_process = nullptr;
    yield();
    // Should never reach here
    return 0;
}
```

Notice that after the kernel stack is freed (as noted in the comment), we theoretically shouldn't call any more functions — but we do. This is a serious problem. With process states, we can set the current process to Zombie, place it in a garbage collection queue, and have a specific process reclaim it later. We'll have process 0 (the idle process) do the reclamation. Why? Because process 0 never terminates, and it doesn't do anything anyway, so there's no interference.

```cpp
extern "C" void kernel_main(multiboot_info_t* mbi) {
    ...
    create_process(reinterpret_cast<void*>(&shell), nullptr);
    
    while (1) {
        do_process_recycle();
        yield();
    }
}
```

The modified process exit logic and reclamation queue logic:

```cpp
void free_pcb(PCB*& process) {
    kfree(reinterpret_cast<void*>(process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(process));
    process = nullptr;
}

uint32_t exit_process(uint8_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    remove_from_scheduling_queue(pid);
    PCB*& cur_process = process_list[pid];
    if (pid != cur_process_id) {
        free_pcb(cur_process);
        return 0;
    } 
    cur_process->state = process_state::ZOMBIE;
    insert_into_process_recycle_queue(cur_process);
    yield();
    // Should never reach here
    return 0;
}
process_queue process_recycle_queue;

void insert_into_process_recycle_queue(PCB* process) {
    insert_into_process_queue(process_recycle_queue, process);
}

void do_process_recycle() {
    while (process_recycle_queue) {
        uint8_t pid = process_recycle_queue->pid;
        remove_from_process_queue(process_recycle_queue, process_recycle_queue->pid);
        free_pcb(process_list[pid]);
    }
}
```

### Locks

Technically, our task for today is complete. Unfortunately, the console display bug hasn't been fixed yet. So I think we should at least implement a spinlock to solve this problem.

```cpp
typedef struct spinlock {
    volatile uint32_t locked = 0;
} spinlock;

void spinlock_acquire(spinlock* lock) {
    while(1) {
        uint32_t old = 1;
        asm volatile ("xchgl %0, %1"
        : "=r"(old), "+m"(lock->locked)
        : "0"(old)
        : "memory"
        );
        if (old == 0) break;
        asm volatile("pause");
    }
}

void spinlock_release(spinlock* lock) {
    lock->locked = 0;
}
```

The core of a spinlock is the `xchgl` atomic instruction, which exchanges two values. By using it to simultaneously read and update the lock's value, we ensure that only one process can enter the locked region at a time.

Finally, we add the spinlock to `terminal_write`:

```cpp
...
#include <kernel/spinlock.h>
...
spinlock tty_lock;
...
void terminal_write(const char* data, size_t size) {
    spinlock_acquire(&tty_lock);
    for (size_t i = 0; i < size; i++) {
        ...
    }
    spinlock_release(&tty_lock);
}
```

Let's check the result.

![image-20260224205456826](../assets/自制操作系统（12）：进程创建、调度与切换（下）/image-20260224205456826.png)

Well, the integrity of each line is still broken, but at least we can output complete characters, and scrolling works fine.

To guarantee line integrity, we'd need to redesign printf to format the output into a buffer, then call a new `terminal_write` that outputs a complete line. But don't be discouraged — we've already done enough for today. The current result is quite satisfactory.

---

### Summary

Today we implemented a better scheduler using MLFQ (Multi-Level Feedback Queue), separated the shell as process 1 (which we'll later move to user space), added support for creating processes with parameters, implemented spinlocks, and used them to lock our TTY to prevent output conflicts between processes. A pretty good day!

Next step: it's time to enter user mode! In the next chapter, we'll jump out of ring 0 into the world of ring 3! We'll create our first user-mode process! Having gotten used to kernel-mode programming, we're sure to run into many obstacles in user space. Regardless, we'll be patient — see you in the next chapter!
