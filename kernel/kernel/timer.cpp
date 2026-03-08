#include <kernel/timer.hpp>
#include <kernel/mm.hpp>
#include <kernel/process.hpp>
#include <kernel/schedule.h>
#include <driver/pit.h>

#include <stdio.h>
#include <priority_queue>
#include <queue>

constexpr uint16_t MAX_TIMER_NUM = 256;

struct TimerCmp {
    bool operator()(const timer_config* a, const timer_config* b) const {
        return a->wake_tick > b->wake_tick;  // a > b → a 优先级更低
    }
};

std::priority_queue<timer_config*, MAX_TIMER_NUM, TimerCmp> pq;
std::queue<timer_config*, MAX_TIMER_NUM> due_timer_queue;

timer_config* register_timer(uint32_t wake_tick, timer_callback_func callback, void* args) {
    if (pq.size() >= MAX_TIMER_NUM) {
        return nullptr;
    }
    timer_config* conf = (timer_config*)kmalloc(sizeof(timer_config));
    if (!conf) return nullptr;
    conf->wake_tick = wake_tick;
    conf->callback_func = callback;
    conf->args = args;
    conf->cancelled = false;
    pq.push(conf);
    return conf;
}

void cancel_timer(timer_config* conf) {
    if (conf) conf->cancelled = true;
}

void kernel_timer_main() {
    while (1) {
        while (!due_timer_queue.empty()) {
            timer_config* conf = due_timer_queue.front();
            due_timer_queue.pop();
            if (!conf->cancelled) {
                conf->callback_func(conf->args);
            }
            kfree(conf);
        }
        yield();
    }
}

void init_kernel_timer() {
    create_process("ktimerd", (void *)(&kernel_timer_main), nullptr); // ktimerd进程
}

void timer_handler(uint32_t current_tick) {
    while (!pq.empty() && pq.top()->wake_tick <= current_tick) {
        due_timer_queue.push(pq.top());
        pq.pop();
    }
}

void sleep(uint32_t ms) {
    uint32_t ms_10 = (ms + 9) / 10;
    pid_t sleep_pid;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    auto callback = [](void* args) -> void {
        pid_t* pid = (pid_t*)args;

        uint32_t flags = spinlock_acquire(&process_list_lock);
        process_list[*pid]->state = process_state::READY;
        spinlock_release(&process_list_lock, flags);

        insert_into_scheduling_queue(*pid);
        return;
    };
    sleep_pid = cur_process_id;
    register_timer(pit_get_ticks() + ms_10, callback, &sleep_pid);
    process_list[cur_process_id]->state = process_state::SLEEPING;
    spinlock_release(&process_list_lock, flags);
    yield();
}