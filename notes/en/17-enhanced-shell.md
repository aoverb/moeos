## Homemade Operating System (17): A Series of Improvements Around the Enhanced Shell

We've implemented the file system... but our shell can't even freely access it! This is unacceptable!

Let's set our goals:

1. Support `cd` to switch the current working directory;
2. When entering a command, match files under `/usr/bin` (preferably configurable), as well as files in the current directory, and pass subsequent arguments as parameters to the file;
3. When running a process, be able to block the shell and return the exit code to the shell.

Let's implement these one by one.

### Matching and Executing Files Under /usr/bin

Let's first see how to make the shell default to matching and executing files under `/usr/bin`.

We build a data structure to simulate the PATH environment variable:

```cpp
constexpr char* PATH[2] = {
    "/usr/bin/",
    "/"
};
```

Then we adapt several system calls: file-related operations, and memory allocation... Wait, we haven't implemented user-mode memory allocation yet! We'll have to read from the stack, hoping our allocated user-mode stack space is sufficient.

![image-20260301024854885](../assets/自制操作系统（17）：增强的Shell/image-20260301024854885.png)

The exec problem: our code isn't in the kernel!

![image-20260301025205956](../assets/自制操作系统（17）：增强的Shell/image-20260301025205956.png)

We can first copy the user code to a kernel buffer, then free it after it's been copied to the new user space.

![image-20260301025302629](../assets/自制操作系统（17）：增强的Shell/image-20260301025302629.png)

This time it ran successfully.

### Passing Arguments to the File

This requires us to upgrade the `create_user_process` function.

We construct Argc, argv addresses on the user stack, with the argv addresses pointing directly deeper into the stack.

Let's try passing arguments when creating a process:

```cpp
            bool flag = false;
            char fn[256] = "/usr/bin/";
            for (int i = 0; i < 2; ++i) {
                int fd = open(strcat(fn, input), 1);
                if (fd == -1) continue;
                int size = read(fd, buffer, 32768);
                if (size == -1) continue;
                printf("Executing %s: %d bytes loaded\n", fn, size);
                char* v[] = {"rumianomnomnom\n", "yay\n", "can you hear me?"};
                exec(buffer, size, 1, 3, v);
                flag = true;
            }
```

![image-20260301033223983](../assets/自制操作系统（17）：增强的Shell/image-20260301033223983.png)

Now we can pass arguments to running programs!

### cwd

We add a new `cwd` field to the PCB to record the current working directory:

Then in the kernel process creation logic, we inherit the parent process's cwd:

```cpp
    strcpy(new_process->cwd, process_list[cur_process_id]->cwd);
```

Then we add two system calls, `chdir` and `getcwd`.

We can then write `pwd` and `ls` programs.

`cd` is implemented as a shell built-in, and we also added a tokenizer for the shell.

![image-20260301043754669](../assets/自制操作系统（17）：增强的Shell/image-20260301043754669.png)

But now we have output contention issues — we need a `waitpid`!

### waitpid

After creating a child process through a spawn-like mechanism, I need to set myself to a waiting state. At this point, I basically have two choices. One is to add a waiting queue data structure in the process PCB — after setting myself to a waiting state, I remove myself from the scheduling queue, then call `schedule`. When the child process exits, `exit_process` traverses the waiting queue and puts the processes back into the scheduling queue. The other option is to enter a loop, and each time I'm scheduled, check the current child process pid — if the child hasn't become a ZOMBIE yet, continue the loop; if it has become a ZOMBIE, take its exit code and reclaim its PCB.

We choose option one here, because option two is essentially busy waiting + polling.

```cpp
    new_process->parent_pid = cur_process_id;
    new_process->waiting_queue = nullptr;
    new_process->fd_num = 0;
    strcpy(new_process->cwd, process_list[cur_process_id]->cwd);

    // Set parent process to waiting state, remove from scheduling queue
    remove_from_scheduling_queue(cur_process_id);
    process_list[cur_process_id]->state = process_state::WAITING;
    // Put into the new process's waiting queue
    insert_into_process_queue(new_process->waiting_queue, process_list[cur_process_id]);

    insert_into_scheduling_queue(newpid, priority);
```

Then when exiting a process, we put the processes in its waiting queue back into the schedulable queue:

```cpp
uint32_t exit_process(uint8_t pid) {
    printf("exiting process %d...\n", pid);
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    
    PCB*& exiting_process = process_list[pid];
    if (pid != cur_process_id) { // If the process to exit is not the current one
        exiting_process->to_exit = 1; // Don't directly clean up this process's space, just tell it it will be exited
        return 0;
    }
    PCB* itr = exiting_process->waiting_queue;
    while (itr) {
        insert_into_scheduling_queue(itr->pid);
        itr = itr->next;
    }
    exiting_process->state = process_state::ZOMBIE;
    remove_from_scheduling_queue(pid);
    // insert_into_process_recycle_queue(cur_process); // no longer needed
    yield();
    // Shouldn't reach here
    return 0;
}
```

Here I also fixed another issue: if the process to exit is not the current process, we shouldn't go and clean up another process's data. Instead, we should send it a signal or something, so that when it gets scheduled later, it exits itself. We added a `to_exit` field, which needs to be checked during initialization and in `schedule`:

```cpp
    process_switch_to(chosen_process->pid);

    flags = process_list[cur_process_id]->saved_eflags;
    asm volatile ("pushl %0; popfl" : : "r"(flags));

    if (cur_pcb->to_exit) {
        exit_process(cur_process_id);
    }
}
```

Then we implement the `waitpid` logic:

```cpp
int waitpid(uint8_t child) {
    if (!process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];
    while(child_pcb->state != process_state::ZOMBIE) {
        yield();
    }
    free_pcb(child_pcb);
    return 0;
}
```

And implement the corresponding system call:

```cpp
// WAITPID(ebx = pid)
int sys_waitpid(interrupt_frame* reg) {
    uint8_t pid = static_cast<uint8_t>(reg->ebx);
    return waitpid(pid);
}
```

Then in the shell:

```cpp
#include <sys/wait.h>
```

The old process recycle queue logic is no longer needed, so we delete it directly.

I also made one more change: changed `process_id` to use the `pid_t` type, switching to `int`. I have a feeling this might cause trouble, but for now there are no bugs after the conversion.

```
asm volatile ("sti");
spinlock_release(&process_list_lock);
```

![image-20260301145034920](../assets/自制操作系统（17）：增强的Shell/image-20260301145034920.png)

The timing of enabling interrupts was wrong, causing a deadlock. Simply swap the two instructions.

![image-20260301145146392](../assets/自制操作系统（17）：增强的Shell/image-20260301145146392.png)

The program can run, but it doesn't correctly schedule back to the shell.

On one hand, we weren't traversing the waiting queue correctly:

![image-20260301145630680](../assets/自制操作系统（17）：增强的Shell/image-20260301145630680.png)

![image-20260301145641733](../assets/自制操作系统（17）：增强的Shell/image-20260301145641733.png)

Whoa — it says we're still in the scheduling queue!

![image-20260301152402530](../assets/自制操作系统（17）：增强的Shell/image-20260301152402530.png)

This changes the state to ready...

`yield` changes the state, so you need to be careful about pairing it with state-change statements.

![image-20260301152721476](../assets/自制操作系统（17）：增强的Shell/image-20260301152721476.png)

![image-20260301152832123](../assets/自制操作系统（17）：增强的Shell/image-20260301152832123.png)

Improvements made.

![image-20260301152849346](../assets/自制操作系统（17）：增强的Shell/image-20260301152849346.png)

Done.

Our improvements:

```
git commit -m "Added waitpid syscall; scheduler adjusted so schedule only re-enqueues RUNNING processes; process_id uses pid_t, switched to int; exit_process when exiting other processes uses a semaphore-like mechanism."
```

![image-20260301153704990](../assets/自制操作系统（17）：增强的Shell/image-20260301153704990.png)

Now there's no more output contention issue.

Discovered that blocking semantics shouldn't be placed inside `create_process`.

```cpp
int waitpid(pid_t child) {
    if (child < 0 || child > MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];
    // Set parent to waiting state, remove from scheduling queue
    process_list[cur_process_id]->state = process_state::WAITING;
    remove_from_scheduling_queue(cur_process_id);
    // Put into child process's waiting queue
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    while(child_pcb->state != process_state::ZOMBIE) {
        yield();
    }
    free_pcb(process_list[child]);
    return 0;
}
```

Exit code adaptation

We need to prepare an `exit_code` in the PCB.

```cpp
uint32_t exit_process(pid_t pid, int exit_code) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    PCB*& exiting_process = process_list[pid];
    if (!exiting_process->to_exit) {
        // Current process hasn't been assigned to exit yet, use the passed exit code
        exiting_process->exit_code = exit_code;
    }
```

This ensures the exit code isn't overwritten.

```cpp
int waitpid(pid_t child) {
    if (child < 0 || child > MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];

    asm volatile("cli");
    if (child_pcb->state == process_state::ZOMBIE) {
        // Child process has already exited, reclaim directly
        asm volatile("sti");
        int exit_code = child_pcb->exit_code;
        free_pcb(process_list[child]);
        return exit_code;
    }
    process_list[cur_process_id]->state = process_state::WAITING;
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    yield();

    int exit_code = child_pcb->exit_code;
    free_pcb(process_list[child]);
    return exit_code;
}
```

We also need to catch the return code here:

```cpp
// CLOSE(ebx = exit_code)
int sys_exit(interrupt_frame* reg) {
    int exit_code = static_cast<int>(reg->ebx);
    exit_process(cur_process_id, exit_code);
    return 0;
}
```

System call, using ebx to pass the value.

```assembly
_start:
    call main
    mov %eax, %ebx
    mov $0, %eax
    int $0x80
    ret
```

crt0 also needs to change — the return code is in eax, so we first copy it to ebx to pass in.

![image-20260302010919652](../assets/自制操作系统（17）：增强的Shell/image-20260302010919652.png)

Now we can do arithmetic!

![image-20260301155829412](../assets/自制操作系统（17）：增强的Shell/image-20260301155829412.png)

![image-20260301155841752](../assets/自制操作系统（17）：增强的Shell/image-20260301155841752.png)

![image-20260301161500722](../assets/自制操作系统（17）：增强的Shell/image-20260301161500722.png)

Later we found it was the PCB being too large, stepping on memory...

![image-20260301225522380](../assets/自制操作系统（17）：增强的Shell/image-20260301225522380.png)

![image-20260301225701484](../assets/自制操作系统（17）：增强的Shell/image-20260301225701484.png)

![image-20260301225720849](../assets/自制操作系统（17）：增强的Shell/image-20260301225720849.png)

Consider this scenario:

```cpp
int waitpid(pid_t child) {
    if (child < 0 || child > MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];
    // Set parent process to waiting state, remove from scheduling queue
    process_list[cur_process_id]->state = process_state::WAITING;
    // Put into child process's waiting queue
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    while(child_pcb->state != process_state::ZOMBIE) {
        process_list[cur_process_id]->state = process_state::WAITING; // Suppose we get scheduled away right before this line
        yield();
    }
    free_pcb(process_list[child]);
    return 0;
}

uint32_t exit_process(pid_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    PCB*& exiting_process = process_list[pid];
    if (pid != cur_process_id) { // If the process to exit is not the current one
        exiting_process->to_exit = 1; // Don't directly clean up this process's space, tell it to exit itself later
        return 0;
    }
    asm volatile("cli");
    PCB* itr;
    while (itr = exiting_process->waiting_queue) {
        itr->state = process_state::READY;
        remove_from_process_queue(exiting_process->waiting_queue, itr->pid);
        insert_into_scheduling_queue(itr->pid); // Put it back in the scheduling queue and set to ready
    }
    exiting_process->state = process_state::ZOMBIE;
    yield();
    // Shouldn't reach here
    return 0;
}


        process_list[cur_process_id]->state = process_state::WAITING; // When scheduled back, sets itself to WAITING again...
        yield();
    }
    free_pcb(process_list[child]);
    return 0;
}

void schedule() {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags));

    PCB* cur_pcb = process_list[cur_process_id];
    cur_pcb->saved_eflags = flags;

    if (cur_pcb->state == process_state::RUNNING) { // When scheduling, won't put itself back in scheduling queue
        insert_into_scheduling_queue(cur_process_id, cur_pcb->priority, false);
        cur_pcb->state = process_state::READY;
        if (--(cur_pcb->quota) == 0) {
            if (cur_pcb->priority > 0) {
                insert_into_scheduling_queue(cur_process_id, cur_pcb->priority - 1);
            }
            cur_pcb->quota = MAP_PRIORITY_TO_QUOTA[cur_pcb->priority];
        }
    }

    Now there are two processes:
        0: Waiting for process 1 to finish
        1: Will never be scheduled
        
  
    if (!chosen_process) {
        chosen_process = process_list[0];
    }
    chosen_process->state = process_state::RUNNING;
    remove_from_scheduling_queue(chosen_process->pid);
    
    So at the end, schedule's fallback mechanism wakes up process 0, and because of the fallback, process 0 keeps running...
        
        if (cur_pcb->state == process_state::RUNNING) {
        insert_into_scheduling_queue(cur_process_id, cur_pcb->priority, false);
        cur_pcb->state = process_state::READY;
        if (--(cur_pcb->quota) == 0) {
            if (cur_pcb->priority > 0) {
                insert_into_scheduling_queue(cur_process_id, cur_pcb->priority - 1);
            }
            cur_pcb->quota = MAP_PRIORITY_TO_QUOTA[cur_pcb->priority];
        }
    }
    if (--resetcnt == 0) {
        resetcnt = RESETCNT_INITIAL;
        move_all_to_top_priority(); <- When we get here, disaster strikes!
    }
        
    This function:
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
        tail->priority = MAX_PRIORITY; // Always assumes the ready queue has something!
        tail->quota = MAP_PRIORITY_TO_QUOTA[MAX_PRIORITY];
        tail = tail->next;
    } while (tail != head);
}
    So process 0 triggers a null pointer error.
```

```gdb
(gdb) list 46
41              sche_queue_head[i - 1] = nullptr;
42          }
43          PCB* head = sche_queue_head[MAX_PRIORITY];
44          PCB* tail = head;
45          do {
46              tail->priority = MAX_PRIORITY;
47              tail->quota = MAP_PRIORITY_TO_QUOTA[MAX_PRIORITY];
48              tail = tail->next;
49          } while (tail != head);
50      }
Breakpoint 1, 0xc01020b8 in isr14 ()
(gdb) n
Single stepping until exit from function isr14,
which has no line number information.
0xc010219e in common_interrupt_handler ()
(gdb)
Single stepping until exit from function common_interrupt_handler,
which has no line number information.
move_all_to_top_priority () at arch/i386/schedule.cpp:46
46              tail->priority = MAX_PRIORITY;
(gdb) print tail
$1 = (PCB *) 0x0
(gdb) where
#0  move_all_to_top_priority () at arch/i386/schedule.cpp:46
#1  0xc0102ef1 in schedule () at arch/i386/schedule.cpp:71
#2  0xc0102fa5 in yield () at arch/i386/schedule.cpp:104
#3  0xc0102b68 in waitpid (child=1) at arch/i386/process.cpp:47
#4  0xc0106dd1 in kernel_main (mbi=0x0) at kernel/kernel.cpp:254
#5  0xc0101155 in _tokernelmain ()
    
    
(gdb) print process_list[1]->state
$2 = process_state::WAITING
(gdb)  print process_list[2]
$3 = (PCB *) 0x0
(gdb) print process_list[0]->state
$4 = process_state::WAITING
(gdb) print process_list[2]->state
Cannot access memory at address 0x30
(gdb) print tail->priority
Cannot access memory at address 0x14
(gdb) print tail
$5 = (PCB *) 0x0
```

There's another scenario: before `schedule` gets to call `waitpid`, the child process has already finished.

```cpp
int waitpid(pid_t child) {
    if (child < 0 || child > MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];

    asm volatile("cli");
    if (child_pcb->state == process_state::ZOMBIE) {
        // Child has already exited, reclaim directly
        asm volatile("sti");
        free_pcb(process_list[child]);
        return 0;
    }
    process_list[cur_process_id]->state = process_state::WAITING;
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    yield();

    free_pcb(process_list[child]);
    return 0;
}
```

This is how it should be fixed.

---

In this section, we made many OS improvements and bug fixes around enhancing the shell. In the next section, we'll implement user-mode malloc/free.
