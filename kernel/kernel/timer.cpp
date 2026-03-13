#include <kernel/timer.hpp>
#include <kernel/mm.hpp>
#include <kernel/process.hpp>
#include <kernel/schedule.h>
#include <driver/pit.h>

#include <stdio.h>
#include <priority_queue>
#include <unordered_map>
#include <queue>

constexpr uint16_t MAX_TIMER_NUM = 256;

struct TimerCmp {
    bool operator()(const timer_config* a, const timer_config* b) const {
        return a->wake_tick > b->wake_tick;  // a > b → a 优先级更低
    }
};

std::priority_queue<timer_config*, MAX_TIMER_NUM, TimerCmp> pq;
std::queue<timer_config*, MAX_TIMER_NUM> due_timer_queue;
spinlock pq_lock;
spinlock dq_lock;

static timer_id_t next_timer_id = 0;

// isvalid 改为 id -> conf* 的映射
spinlock validmapLock;
static std::unordered_map<timer_id_t, timer_config*> valid_timers;

timer_id_t register_timer(uint32_t wake_tick, timer_callback_func callback, void* args) {
    SpinlockGuard guard(pq_lock);
    if (pq.size() >= MAX_TIMER_NUM) {
        return 0;  // 0 表示无效 ID
    }
    timer_config* conf = (timer_config*)kmalloc(sizeof(timer_config));
    if (!conf) return 0;

    timer_id_t id = ++next_timer_id;

    conf->id = id;
    conf->pid = cur_process_id;
    conf->wake_tick = wake_tick;
    conf->callback_func = callback;
    conf->args = args;

    {
        SpinlockGuard guard(validmapLock);
        valid_timers[id] = conf;
    }
    pq.push(conf);
    return id;
}

void cancel_timer(timer_id_t id) {
    if (id == 0) return;
    SpinlockGuard guard(validmapLock);
    auto it = valid_timers.find(id);
    if (it == valid_timers.end()) {
        return;
    }
    valid_timers.erase(it);
}

void kernel_timer_main() {
    while (1) {
        timer_config* conf = nullptr;
        {
            SpinlockGuard guard(dq_lock);
            if (!due_timer_queue.empty()) {
                conf = due_timer_queue.front();
                due_timer_queue.pop();
            }
        }
        if (conf) {
            {
                SpinlockGuard guard(validmapLock);
                auto it = valid_timers.find(conf->id);
                if (it != valid_timers.end()) {
                    conf->callback_func(conf->pid, conf->args);
                    valid_timers.erase(it);
                }
            }
            kfree(conf);
        } else {
            yield();
        }
    }
}

void init_kernel_timer() {
    create_process(KERNEL_PROC_NAME_KTIMER, (void *)(&kernel_timer_main), nullptr); // ktimerd进程
}

void timer_handler(uint32_t current_tick) {
    SpinlockGuard pqguard(pq_lock);
    SpinlockGuard dqguard(dq_lock);
    while (!pq.empty() && pq.top()->wake_tick <= current_tick) {
        due_timer_queue.push(pq.top());
        pq.pop();
    }
}

void sleep(uint32_t ms) {
    uint32_t ms_10 = (ms + 9) / 10;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    auto callback = [](pid_t pid, void*) -> void {
        uint32_t flags = spinlock_acquire(&process_list_lock);
        if (!process_list[pid]) {
            spinlock_release(&process_list_lock, flags);
            return;
        }
        process_list[pid]->state = process_state::READY;
        spinlock_release(&process_list_lock, flags);
        insert_into_scheduling_queue(pid);
    };
    register_timer(pit_get_ticks() + ms_10, callback, nullptr);
    process_list[cur_process_id]->state = process_state::SLEEPING;
    spinlock_release(&process_list_lock, flags);
    yield();
}

void timeout(process_queue* queue, uint32_t ms) {
    uint32_t ms_10 = (ms + 9) / 10;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    auto callback = [](pid_t pid, void* queue) -> void {
        uint32_t flags = spinlock_acquire(&process_list_lock);
        if (!process_list[pid] ||
            process_list[pid]->state != process_state::WAITING) {
            spinlock_release(&process_list_lock, flags);
            return;
        }
        process_list[pid]->state = process_state::READY;
        process_queue& pq = *((process_queue*)queue);
        remove_from_waiting_queue(pq, pid);
        spinlock_release(&process_list_lock, flags);
        insert_into_scheduling_queue(pid);
    };
    timer_id_t id;
    if (ms != 0) {
        id = register_timer(pit_get_ticks() + ms_10, callback, queue);
    }
    process_list[cur_process_id]->state = process_state::WAITING;
    spinlock_release(&process_list_lock, flags);
    yield();
    if (ms != 0) {
        cancel_timer(id);
    }
}
