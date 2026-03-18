#include "idt.h"
#include <stdio.h>
#include <string.h>
#include <kernel/ksignal.h>
#include <kernel/schedule.hpp>
#include <kernel/process.hpp>

inline uint32_t lowbit(uint32_t n) { return n & (-n);}
constexpr uint32_t MAX_SIGNAL = 32;
signal_handler_t signal_handler_table[MAX_SIGNAL] = { nullptr };

extern "C" void do_signal(registers* regs);

void register_signal(uint8_t n, signal_handler_t handler) {
    signal_handler_table[n] = handler;
}

void sigint_handler(registers*, pid_t pid) {
    exit_process(pid, 3); // todo: 也许应该规范一下退出码的含义...
    return;
}

void signal_init() {
    printf("signal initializing...");
    register_signal(uint32_t(SIGINT), sigint_handler);
    printf("OK\n");
}

bool send_signal(pid_t pid, uint32_t sig) {
    SpinlockGuard guard(process_list_lock);
    if (!process_list[pid]) {
        return false;
    }
    process_list[pid]->signal |= (1 << sig);
    return true;
}

void do_signal(registers* regs) {
    uint32_t pending;
    {
        pending = process_list[cur_process_id]->signal;
        process_list[cur_process_id]->signal = 0;
    }
    while (pending) {
        int sig = __builtin_ctz(pending);
        pending &= ~(1u << sig);

        if (signal_handler_table[sig]) {
            signal_handler_table[sig](regs, cur_process_id);
        }
        {
            if (!process_list[cur_process_id] ||
                process_list[cur_process_id]->state == process_state::ZOMBIE) {
                break;
            }
        }
    }
}