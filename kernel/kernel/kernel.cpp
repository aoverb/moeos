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
#include <syscall_def.h>

#include <driver/keyboard.h>
#include <driver/pit.h>

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

void print_rumia_text() {
    set_color(0xF0B526);
    printf("Rumi");
    set_color(0xEB392D);
    printf("a");
    set_color(0xFFFFFF);
}

void print_lolios() {
    set_color(0xEB9D2F);
    printf("LoliOS");
    set_color(0xFFFFFF);
}

void pmm_prepare(multiboot_info_t* mbi) {
    uint64_t kernel_begin = 0x100000;
    uint64_t kernel_end = (uint64_t)(&_kernel_end);

    uint32_t lfb_begin = mbi->framebuffer_addr;
    uint32_t lfb_end = lfb_begin + mbi->framebuffer_pitch * mbi->framebuffer_height - 1;

    struct { uint64_t begin; uint64_t end; } reserved[] = {
        { kernel_begin, kernel_end },
        { lfb_begin, lfb_end },
        // multiboot_info 结构体本身
        { (uint32_t)(uintptr_t)mbi, (uint32_t)(uintptr_t)mbi + sizeof(multiboot_info_t) - 1 },
        // multiboot 内存映射表
        { mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length - 1 },
    };
    constexpr int num_reserved = sizeof(reserved) / sizeof(reserved[0]);

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

void proc1(void* args) {
    char* s = reinterpret_cast<char*>(args);
    for (uint32_t i = 0; i < 99999999; i++) {
        if (i % 10000000 == 0) printf("%s %d:%d/99999999\n", s, cur_process_id, i);
    }
}

void print_tick() {
    uint8_t ticks = 0;
    while (++ticks) {
        printf("%d ", ticks);
        pit_sleep(1000);
    }
}

void shell() {
    char input[256];
    while (1) {
        print_lolios();
        
        printf(">");
        getline(input, 256);
        
        if (strcmp(input, "help") == 0) {
            printf("Hello user!");
            printf("This is ");
            print_lolios();
            printf("!\n");
            printf("The host here is ");
            print_rumia_text();
            printf("! Feel free!\n");
        } else if (strcmp(input, "rumia") == 0) {
            print_rumia();
        } else if (strcmp(input, "") == 0) {
            continue;
        } else if (strcmp(input, "exit") == 0) {
            print_rumia_text();
            printf(": Goodbye, aoverb!\n");
            break;
        } else if (strcmp(input, "test") == 0) {
            uint32_t* test_array = reinterpret_cast<uint32_t*>(kmalloc(8));
            
            for (uint32_t i = 0; i < 8; i++) {
                test_array[i] = i;
            }

            for (uint32_t i = 0; i < 8; i++) {
                printf("%d ", test_array[i]);
            }

            kfree(test_array);
        } else if (strcmp(input, "probe") == 0) {
            pmm_probe();
        } else if (strcmp(input, "ps") == 0) {
            print_process();
        }else if (strcmp(input, "halt") == 0) {
            printf("HALT!");
            asm volatile("hlt");
        } else if (strcmp(input, "time") == 0) {
            printf("%d", pit_get_ticks());
        } else if (strcmp(input, "tick") == 0) {
            create_process(reinterpret_cast<void*>(&print_tick), nullptr);
        } else if (strcmp(input, "kill2") == 0) {
            exit_process(2);
        }  else {
            print_rumia_text();
            printf(": Unknown command '%s'!\n", input);
        }
        
        printf("\n");
    }
}

typedef struct {
    uint32_t mod_start;   // 模块在内存中的起始物理地址
    uint32_t mod_end;     // 模块结束物理地址
    uint32_t cmdline;     // 模块命令行字符串（就是 grub.cfg 里的路径）
    uint32_t pad;         // 保留，为 0
} multiboot_module_t;

extern "C" void kernel_main(multiboot_info_t* mbi) {
    pmm_prepare(mbi);
    vmm_init();
    terminal_initialize(mbi);
    kheap_init();
    // vmm_cleanup_low_identity_mapping(); // 到这里清除了低地址的恒等映射，mbi就失效了
    // todo: pmm部分数据需重新映射，才能安全清除恒等映射
    // mbi = NULL;
    print_rumia();
    printf("HAL initializing...");
    hal_init();
    keyboard_init();
    pit_init();
    syscall_init();
    process_init();
    asm volatile ("sti");
    
    printf("OK\n");
    printf("Welcome, aoverb!\n\n");
    printf("The kernel_main lies in %X, sounds great!\n\n", &kernel_main);
    create_process(reinterpret_cast<void*>(&shell), nullptr);

    if (mbi->flags & (1 << 3)) {  // 检查 mods 字段有效
        multiboot_module_t* mods = (multiboot_module_t*)mbi->mods_addr;
        uint32_t mod_count = mbi->mods_count;

        for (uint32_t i = 0; i < mod_count; i++) {
            void* start = (void*)mods[i].mod_start;
            size_t size = mods[i].mod_end - mods[i].mod_start;
            const char* name = (const char*)mods[i].cmdline;

            create_user_process(start, size, 1);
        }
    }

    while (1) {
        do_process_recycle();
        yield();
    }
}
