/* kernel/kernel/kernel.cpp */
#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <string>
// 引入上面定义的两个头文件
#include <boot/multiboot.h>
#include <stdio.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/hal.h>
#include <kernel/mm.hpp>
#include <kernel/schedule.hpp>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/ksignal.h>
#include <kernel/panic.h>
#include <kernel/net/net.hpp>
#include <kernel/timer.hpp>
#include <syscall_def.hpp>

#include <driver/keyboard.h>
#include <driver/pit.h>
#include <driver/ata.hpp>
#include <driver/vfs.hpp>
#include <driver/tarfs.hpp>
#include <driver/devfs.hpp>
#include <driver/pipefs.hpp>
#include <driver/sockfs.hpp>
#include <driver/rtl8139.hpp>
#include <driver/block.hpp>
#include <driver/ext2.hpp>
#include <driver/procfs.hpp>

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
    printf("|     :+@*#-%@=:#+  :-%@@@@%%@-  .==*.@**=%*%@@=- :%@*+@@@@@:*-@=::*%=-=@@*:  |\n");
    printf("|     :#@@*=+%=#%=    #@@@@@@@-    -#=@@@%@%@@%#-:=@@+-@%=-*:*:*.@% *.=*@@@-. |\n");
    printf("|     -@*@@@@-.+@%:==-@@@@@@@@#:==:%%*@@@@@=@@+%=.-@@+:*=-+-:* #.*=:#+= @@%=  |\n");
    printf("|    .-++@%@@- *%%%%%@@@*###*%@@%#%%+%%@@@%.@@+-= .=@%**%**@#%*@%**@@**%@%=.  |\n");
    printf("|    .---@+%@+ +@@@@@@@#=@@@%+@@@@%=++@@@%+ #@=-:  .-*@@@@@@@@@@@@@@@@@%+:.   |\n");
    printf("|     :::*=-@#  =%@@@@@@+@@@@*@@@@@#-@@@-*.-#*:..   .=@#*#%@@@@@@@@%*+-..     |\n");
    printf("|        .:--*.:  :-+*#%%#%@%@%#+=: -%+.-:*+-.     ..:.....::----:..:.        |\n");
    printf("|          :.:.=:.+.   ++-*@*=*+.-...==+%#%%=.                                |\n");
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

void test_unordered_map() {
    std::unordered_map<int, int> m;
    m[1] = 10; m[2] = 20;
    printf("kernel: m[1]=%d m[2]=%d size=%zu\n", m[1], m[2], m.size());

    std::unordered_map<std::string, int> s;
    char ss[9];
    ss[0] = 'i';
    ss[1] = 'l';
    ss[2] = 'y';
    ss[3] = '\0';
    s[ss] = 42;
    printf("kernel: s[\"test\"]=%d\n", s["ily"]);

    printf("test_unordered_map KERNEL MODE OK\n");
}

extern void init_console_dev(mounting_point* mp);
extern void init_nic_dev_file(mounting_point* mp);
extern void init_ipv4addr_dev_file(mounting_point* mp);
extern void mm_reg_in_procfs(mounting_point* mp);
extern void set_proc_mp(mounting_point* mp);
void fs_init(saved_module* saved, uint32_t mod_count) {
    printf("filesystem initializing...\n");
    init_vfs();
    init_tarfs();
    init_devfs();
    init_pipefs();
    init_sockfs();
    init_ext2fs();
    init_procfs();

    tarfs_metadata tarmeta;
    for (uint32_t i = 0; i < mod_count; i++) {
        tarmeta.data = saved[i].data;
        tarmeta.size = saved[i].size;
        mounting_point* ret = v_mount(FS_DRIVER::TARFS, "/", &tarmeta);
        if (ret == nullptr) {
            panic("failed to mount root!");
        } else {
            printf("Root mounted!\n");
        }
    }

    // 现在我们自己知道只有一块盘，可以强行指定挂载，后面有多块盘就得想办法适配了
    for (int i = 0; i < bdev_cnt; ++i) {
        if (bdev_list[i]->fs == file_system::EXT2) {
            ext2_data* data = (ext2_data*)kmalloc(sizeof(ext2_data));
            data->dev = bdev_list[i];
            mounting_point* ret = v_mount(FS_DRIVER::EXT2FS, "/ext2", data);
            if (ret == nullptr) {
                printf("failed to mount ext2 block device to /ext2!\n");
            } else {
                printf("/ext2 mounted!\n");
            }
            break;
        }
    }

    mounting_point* dev_ret = v_mount(FS_DRIVER::DEVFS, "/dev", nullptr);
    if (dev_ret == nullptr) {
        panic("failed to mount devfs to /dev!");
    } else {
        printf("/dev mounted!\n");
    }
    init_console_dev(dev_ret);
    init_nic_dev_file(dev_ret);
    init_ipv4addr_dev_file(dev_ret);

    mounting_point* pipe_ret = v_mount(FS_DRIVER::PIPEFS, "/pipe", nullptr);
    if (pipe_ret == nullptr) {
        panic("failed to mount pipefs to /pipe!");
    } else {
        printf("/pipe mounted!\n");
    }

    mounting_point* sock_ret = v_mount(FS_DRIVER::SOCKFS, "/sock", nullptr);
    if (sock_ret == nullptr) {
        panic("failed to mount sockfs to /sock!");
    } else {
        printf("/sock mounted!\n");
    }

    mounting_point* proc_ret = v_mount(FS_DRIVER::PROCFS, "/proc", nullptr);
    if (proc_ret == nullptr) {
        panic("failed to mount procfs to /proc!");
    } else {
        printf("/proc mounted!\n");
    }
    mm_reg_in_procfs(proc_ret);
    set_proc_mp(proc_ret);

    printf("filesystem initialized!\n");
}

void start_telnetd_svc() {
    PCB* cur_pcb = process_list[cur_process_id];

    int fd = v_open(cur_pcb, "/usr/bin/telnetd", 1);
    if (fd == -1) {
        printf("failed to start telnetd!\n");
        return;
    }
    char* buffer = (char*)kmalloc(131072);
    int size = v_read(cur_pcb, fd, buffer, 131072);
    pid_t telnetd_pid = exec("shell", buffer, size, 1, 0, nullptr);
    if (telnetd_pid == 0) {
        printf("failed to start telnetd!\n");
    } else {
        printf("Telnet service started!\n");
    }
}

void start_shell() {
    PCB* cur_pcb = process_list[cur_process_id];

    int fd = v_open(cur_pcb, "/usr/bin/shell", 1);
    if (fd == -1) {
        panic("failed to open shell!");
    }
    char* buffer = (char*)kmalloc(131072);
    int size = v_read(cur_pcb, fd, buffer, 131072);
    while(1) {
        pid_t shell_pid = exec("shell", buffer, size, 1, 0, nullptr);
        terminal_setforeground(shell_pid);
        if (shell_pid == 0) panic("Loading shell failed!");
        waitpid(shell_pid);
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

    hal_init();
    keyboard_init();
    pit_init();
    syscall_init();
    ata_init();
    block_init();
    fs_init(saved, mod_count);
    process_init();
    signal_init();
    rtl8139_init();
    init_netconf();
    init_kernel_timer();
    asm volatile ("sti");
    start_telnetd_svc();
    start_shell();

    while (1) {
        yield();
    }
}
