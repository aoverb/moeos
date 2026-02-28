/* kernel/kernel/kernel.cpp */
#include <stddef.h>
#include <stdint.h>
// 引入上面定义的两个头文件
#include <boot/multiboot.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/hal.h>
#include <kernel/mm.h>
#include <kernel/schedule.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/panic.h>
#include <syscall_def.h>

#include <driver/keyboard.h>
#include <driver/pit.h>
#include <driver/vfs.h>
#include <driver/tarfs.h>

void print_rumia() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
    printf("-------------------------------------------------------------------------------\n");
    printf("|        :*@@@@@@@%@@*@@@@@@@@@%@@@@@@%*-  ..    #%  :                        |\n");
    printf("|       :*%@@@@@@*@@%=@@@@@@@@@+@@@@@@@@:       .%*  :                        |\n");
    printf("|      :*%#@@@@@%*@%=+@@@@@@@@@:%@@@@@@@%.  ..  *%. :.                        |\n");
    printf("|     .+@*@@@@@@=##-#+@@@@@@%%#==##*@@@@@+*.   :@. :                          |\n");
    printf("|    .-@#*@@@@@#:%+%@=@*@@@@#*+@*#@-#%@@@@+#.  *%  =       ......:            |\n");
    printf("|   .:#+.#@@@@@=#++%@*#-%@@@*=+@%*#+=@@@@@+@%==+. :   ..:=+********=-..       |\n");
    printf("|  .::.:=#@@@@@+#::--*+=+#@@+++--:.:#+@@@*##@@#-..  :=+%@@@@@@@@@@@@@@#+:.    |\n");
    printf("|      -@+@*@@*-.+. -+%#@#*#*@+  :*+.:+%@-*+@@@:. .:*@@@@@@@@@@@@@@@@@@@@+:   |\n");
    printf("|     :+@*#-%@=:#+  :-%@@@@%%@-  .==*.@**=%*%@@=- :%@*+@@@@@:*-@=::*%--=@@*:  |\n");
    printf("|     :#@@*=+%-#%=    #@@@@@@@-    -#=@@@%@%@@%#-:=@@+-@%--*:*:*.@% *.=*@@@-. |\n");
    printf("|     -@*@@@@-.+@%:==-@@@@@@@@#:==:%%*@@@@@=@@+%=.-@@+:*=-+-:* #.*=:#+= @@%-  |\n");
    printf("|    .-++@%@@- *%%%%%@@@*###*%@@%#%%+%%@@@%.@@+-= .=@%**%**@#%*@%**@@**%@%-.  |\n");
    printf("|    .---@+%@+ +@@@@@@@#=@@@%+@@@@%=++@@@%+ #@=-:  .-*@@@@@@@@@@@@@@@@@%+:.   |\n");
    printf("|     :::*=-@#  =%@@@@@@+@@@@*@@@@@#-@@@-*.-#*:..   .=@#*#%@@@@@@@@%*+-..     |\n");
    printf("|        .:--*.:  :-+*#%%#%@%@%#+=: -%+.-:*+-.     ..:.....::----:..:.        |\n");
    printf("|          :.:.=:.+.   ++-*@*=*+.-...==+%#%%-.                                |\n");
    printf("|                .-=+%.*@%=:*@@=-@#@@%%%=%=**-.                               |\n");
    printf("|             .-*#@@@* .=+==*--  #@@@@@@+@+%@+-.                              |\n");
    printf("|         .::=-@@@@@@=  =%=+#-   .=+*%@@+#-++--                               |\n");
    printf("|         .-*+#*@@@#=   .- .:     ..:.:-... .                                 |\n");
    printf("|          .-===++:..              :.                                         |\n");
    printf("-------------------------------------------------------------------------------\n\n");
#pragma GCC diagnostic pop
}

typedef struct {
    uint32_t mod_start;   // 模块在内存中的起始物理地址
    uint32_t mod_end;     // 模块结束物理地址
    uint32_t cmdline;     // 模块命令行字符串（就是 grub.cfg 里的路径）
    uint32_t pad;         // 保留，为 0
} multiboot_module_t;

void pmm_prepare(multiboot_info_t* mbi) {
    uint64_t kernel_begin = 0x100000;
    uint64_t kernel_end = (uint64_t)(&_kernel_end);

    uint32_t lfb_begin = mbi->framebuffer_addr;
    uint32_t lfb_end = lfb_begin + mbi->framebuffer_pitch * mbi->framebuffer_height - 1;

    struct { uint64_t begin; uint64_t end; } reserved[128];
    int num_reserved = 0;

    reserved[num_reserved++] = { kernel_begin, kernel_end };
    reserved[num_reserved++] = { lfb_begin, lfb_end };
    reserved[num_reserved++] = { (uint32_t)(uintptr_t)mbi, (uint32_t)(uintptr_t)mbi + sizeof(multiboot_info_t) - 1 };
    reserved[num_reserved++] = { mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length - 1 };

    if (mbi->flags & (1 << 3)) {
        multiboot_module_t* mods = (multiboot_module_t*)mbi->mods_addr;
        // reserve mods 数组本身
        reserved[num_reserved++] = {
            (uint64_t)(uintptr_t)mods,
            (uint64_t)(uintptr_t)mods + sizeof(multiboot_module_t) * mbi->mods_count - 1
        };
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            reserved[num_reserved++] = { mods[i].mod_start, mods[i].mod_end - 1 };
        }
    }

    for (int i = 0; i < num_reserved - 1; i++)
        for (int j = i + 1; j < num_reserved; j++)
            if (reserved[i].begin > reserved[j].begin) {
                auto tmp = reserved[i];
                reserved[i] = reserved[j];
                reserved[j] = tmp;
            }

    pm_list pms;
    pms.count = 0;

    auto pm_add_list = [&](uint64_t begin, uint64_t end) {
        constexpr uint64_t mask = (1 << 12) - 1;

        if (begin & mask)
            begin = (begin + mask) & ~mask;
        end = (end + 1) & ~mask;
        if (end == 0) return;
        end -= 1;

        if (end > 0xFFFFFFFF) end = 0xFFFFFFFF;
        if (begin >= end + 1) return;

        pms.entries[pms.count].begin = static_cast<uint32_t>(begin);
        pms.entries[pms.count++].end = static_cast<uint32_t>(end);
    };

    multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)mbi->mmap_addr;

    while ((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t cur_begin = mmap->addr;
            uint64_t cur_end = mmap->addr + mmap->len - 1;

            uint64_t scan = cur_begin;
            for (int i = 0; i < num_reserved && scan <= cur_end; i++) {
                uint64_t r_begin = reserved[i].begin;
                uint64_t r_end = reserved[i].end;

                if (r_end < scan) continue;
                if (r_begin > cur_end) break;

                if (r_begin > scan) {
                    pm_add_list(scan, r_begin - 1);
                }
                scan = r_end + 1;
            }

            if (scan <= cur_end) {
                pm_add_list(scan, cur_end);
            }
        }

        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }

    pmm_init(&pms);
}

void print_tick() {
    uint8_t ticks = 0;
    while (++ticks) {
        printf("%d ", ticks);
        pit_sleep(1000);
    }
}

typedef struct {
    void*  data;
    size_t size;
} saved_module;

void save_module(multiboot_info_t* mbi, saved_module*& saved, uint32_t& mod_count) {
    mod_count = 0;
    saved = nullptr;
    if (mbi->flags & (1 << 3)) {
        multiboot_module_t* mods = (multiboot_module_t*)mbi->mods_addr;
        mod_count = mbi->mods_count;
        saved = (saved_module*)kmalloc(sizeof(saved_module) * mod_count);
        for (uint32_t i = 0; i < mod_count; i++) {
            saved[i].size = mods[i].mod_end - mods[i].mod_start;
            saved[i].data = kmalloc(saved[i].size);
            memcpy(saved[i].data, (void*)mods[i].mod_start, saved[i].size);
        }
    }
}

extern "C" void kernel_main(multiboot_info_t* mbi) {
    pmm_prepare(mbi);
    vmm_init();
    terminal_initialize(mbi);
    kheap_init();
    uint32_t mod_count = 0;
    saved_module* saved;
    save_module(mbi, saved, mod_count);
    pmm_migrate_to_high();
    vmm_cleanup_low_identity_mapping();
    mbi = NULL;
    print_rumia();

    printf("HAL initializing...");
    hal_init();
    printf("OK\n");

    printf("Keyboard initializing...");
    keyboard_init();
    printf("OK\n");

    printf("pit initializing...");
    pit_init();
    printf("OK\n");

    printf("syscall initializing...");
    syscall_init();
    printf("OK\n");

    printf("process initializing...");
    process_init();
    asm volatile ("sti");

    init_vfs();
    init_tarfs();

    printf("OK\n");
    printf("Welcome, aoverb!\n\n");
    printf("The kernel_main lies in %X, sounds great!\n\n", &kernel_main);

    int ret = v_mount(FS_DRIVER::TARFS, "/", nullptr); // 假设我这里准备好了一块tarfs_data传进去
    if (ret < 0) {
        panic("failed to mount root!");
    }

    for (uint32_t i = 0; i < mod_count; i++) {
        create_user_process(saved[i].data, saved[i].size, 1);
        kfree(saved[i].data);
    }
    kfree(saved);

    while (1) {
        do_process_recycle();
        yield();
    }
}
