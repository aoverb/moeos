#include <elf.h>
#if defined(__is_libk)
#include <kernel/mm.h>
#else
#include <stdlib.h>
#endif
#include <string.h>
#include <stdio.h>

void* elf_malloc(uint32_t size) {
#if defined(__is_libk)
    return pmm_alloc(size);
#else
	return malloc(size);
#endif
}

void elf_mmap(uintptr_t p_addr, uintptr_t v_addr, uint32_t flag){
#if defined(__is_libk)
    vmm_map_page(p_addr, v_addr, flag);
#else
	return; // todo: mmap
#endif
}

int verify_elf(void* elf_image, uint32_t size) {
    if (!elf_image || size < sizeof(Elf32_Ehdr))
        return 0;

    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)elf_image;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
        return 0;

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
        return 0;

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
        return 0;

    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
        return 0;

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_REL) // 不支持重定向
        return 0;

    if (ehdr->e_machine != EM_386) // x86
        return 0;

    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr)) // Elf Header大小
        return 0;

    if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf32_Phdr) > size) // 越界检查
        return 0;

    return 1;
}

#define VMM_PAGE_PRESENT  (1 << 0)   /* P: 位 0，1 表示页面在内存中 */
#define VMM_PAGE_WRITABLE (1 << 1)   /* R/W: 位 1，1 表示可读写，0 表示只读 */
#define VMM_PAGE_USER     (1 << 2)   /* U/S: 位 2，1 表示用户态可访问，0 表示仅内核 */

uint32_t elf_to_vmm_flags(uint32_t p_flags) {
    uint32_t vmm_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
    if (p_flags & PF_W) vmm_flags |= VMM_PAGE_WRITABLE;
    return vmm_flags;
}

int construct_user_space_by_elf_image(void* elf_image, uint32_t size, uint32_t& entry, uint32_t &heap_addr) {
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)elf_image;
    // 我们始终假设在调用construct_user_space_by_elf_image之前，你已经调用了verify_elf进行检查

    // 按照ELF文件头的描述，找到段的开头和边界
    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff);
    uint16_t phnum = ehdr->e_phnum;
    heap_addr = 0;
    // 用Program header去遍历解析段数据；
    for (uint32_t i = 0; i < phnum; ++i) {
        phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD) continue;
        if (phdr->p_offset + phdr->p_filesz > size) {
            // 用户态下需要自己回收内存，这里还没有实现用户态，先标记todo
            return 0;
        }
        // 解析到类型为PT_LOAD的段，就按照指引去复制数据块里面对应的块到指定地址
        // 看看实际加载需要多大，按页对齐
        // 映射页要求物理地址和虚拟地址都要按页对齐
        // 我们的物理地址无所谓，但是elf提供的虚拟地址不一定会按页对齐！
        // 所以我们要确认每一块的虚拟地址的起始和终点，按页对齐
        uintptr_t aligned_load_vaddr = phdr->p_vaddr & ~0xFFF;
        uintptr_t aligned_load_vaddr_end = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFF;
        
        uint32_t load_size = aligned_load_vaddr_end - aligned_load_vaddr; 
        void* load_paddr = elf_malloc(load_size);
        uint32_t load_flag = elf_to_vmm_flags(phdr->p_flags);

        for (uintptr_t offset = 0; offset < load_size / 0x1000; ++offset) {
            elf_mmap((uintptr_t)load_paddr + offset * 0x1000, aligned_load_vaddr + offset * 0x1000, load_flag);
        }
            
        memcpy((void*)phdr->p_vaddr, (void*)((uint32_t)elf_image + phdr->p_offset), phdr->p_filesz);

        // 遇到读取大小和段大小不一致的情况就代表是.bss，需要手动置零
        if (phdr->p_filesz < phdr->p_memsz) {
            memset((void*)((uint32_t)phdr->p_vaddr + phdr->p_filesz), 0, phdr->p_memsz - phdr->p_filesz);
        }
        heap_addr = heap_addr < aligned_load_vaddr_end ? aligned_load_vaddr_end : heap_addr;
    }
    entry = ehdr->e_entry;
    return heap_addr > 0 ? 1 : 0;
}