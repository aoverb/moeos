#include <kernel/schedule.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <stdio.h>

void yield() {
    for (uint8_t i = 1; i < MAX_PROCESSES_NUM; ++i) {
        if (i == cur_process_id) continue;
        if (process_list[i]) {
            process_switch_to(i);
            return;
        }
    }
    // 实在没有能切换的进程了，就切回到0号进程
    process_switch_to(0);
    return;
}