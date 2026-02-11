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
    uint32_t lfb_end = lfb_begin + mbi->framebuffer_width * mbi->framebuffer_height * (mbi->framebuffer_bpp / 8) - 1;
    
    pm_list pms;
    auto pm_add_list = [&] (uint64_t begin, uint64_t end) {
        constexpr uint64_t mask = (1 << 12) - 1;
        
        if (mask & begin) {
            begin &= ~mask;
            begin += mask + 1;
        }
        if (mask & end) {
            end &= ~mask;
            end -= mask + 1;
        }
        
        if (end > 0xFFFFFFFF) end = 0xFFFFFFFF;
        if (end - begin <= 0) return;
        printf("free mem: [%lu, %lu]\n", begin, end);
        pms.entries[pms.count].begin = static_cast<uint32_t>(begin);
        pms.entries[pms.count++].end = static_cast<uint32_t>(end);
    };

    multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)mbi->mmap_addr;

    pms.count = 0;
    
    while((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t cur_begin = mmap->addr;
            uint64_t cur_end = mmap->addr + mmap->len - 1;
            if ((kernel_begin <= cur_begin && cur_begin <= kernel_end) || (kernel_begin <= cur_end && cur_end <= kernel_end)) {
                if ((kernel_begin <= cur_begin && cur_begin <= kernel_end) && (kernel_begin <= cur_end && cur_end <= kernel_end)) {
                    continue;
                } else if (kernel_begin <= cur_begin && cur_begin <= kernel_end) {
                    pm_add_list(kernel_end + 1, cur_end);
                } else {
                    pm_add_list(cur_begin, kernel_begin - 1);
                }
            } else if ((cur_begin <= kernel_begin && kernel_end >= cur_begin) && (kernel_begin <= cur_end && cur_end >= kernel_end)) {
                pm_add_list(cur_begin, kernel_begin - 1);
                pm_add_list(kernel_end + 1, cur_end);
            } else if ((lfb_begin <= cur_begin && cur_begin <= lfb_end) || (lfb_begin <= cur_end && cur_end <= lfb_end)) {
                if ((lfb_begin <= cur_begin && cur_begin <= lfb_end) && (lfb_begin <= cur_end && cur_end <= lfb_end)) {
                    continue;
                } else if (lfb_begin <= cur_begin && cur_begin <= lfb_end) {
                    pm_add_list(lfb_end + 1, cur_end);
                } else {
                    pm_add_list(cur_begin, lfb_begin - 1);
                }
            } else if ((cur_begin <= lfb_begin && lfb_end >= cur_begin) && (lfb_begin <= cur_end && cur_end >= lfb_end)) {
                pm_add_list(cur_begin, lfb_begin - 1);
                pm_add_list(lfb_end + 1, cur_end);
            } else {
                pm_add_list(cur_begin, cur_end);
            }
        }
        // mmap->size: 根据规范，这个字段存储的是“除了 size 字段本身以外，该条目剩余部分的大小”。
        // 把size加上就是整个结构体的大小了。Multiboot考虑到未来会在这个结构体的末尾增加新的字段，因此规定要用这种方式去遍历。
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + sizeof(mmap->size));
    }

    pmm_init(&pms);
}

extern "C" void kernel_main(multiboot_info_t* mbi) {
    terminal_initialize(mbi);
    print_rumia();
    pmm_prepare(mbi);
    vmm_init();
    
    printf("HAL initializing...");
    hal_init();
    keyboard_init();
    pit_init();
    asm volatile ("sti");
    printf("OK\n");
    printf("Welcome, aoverb!\n\n");
    printf("The kernel_main lies in %X, sounds great!\n\n", &kernel_main);
    char input[256];


    void* m = pmm_alloc(4096);
    vmm_map_page(reinterpret_cast<uintptr_t>(m), 0xBEEF0000, 0x3);
    uint32_t* test_array = reinterpret_cast<uint32_t*>(0xBEEF0000);
    
    for (uint32_t i = 0; i < 1024; i++) {
        test_array[i] = i;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        printf("%d ", test_array[i]);
    }
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
            uint32_t x = 0;
            void* m = pmm_alloc(4096);
            pmm_free(m);
            printf("%x", m);
        } else if (strcmp(input, "probe") == 0) {
            pmm_probe();
        }else if (strcmp(input, "halt") == 0) {
            printf("HALT!");
            asm volatile("hlt");
        } else if (strcmp(input, "time") == 0) {
            printf("%d", pit_get_ticks());
        } else if (strcmp(input, "tick") == 0) {
            uint8_t ticks = 0;
            while (++ticks) {
                printf("%d ", ticks);
                pit_sleep(1000);
            }
        } else {
            print_rumia_text();
            printf(": Unknown command '%s'!\n", input);
        }
        
        printf("\n");
    }
}
