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

    if (ehdr->e_type != ET_EXEC) // 不支持重定向
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

    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff);
    uint16_t phnum = ehdr->e_phnum;
    heap_addr = 0;

    printf("[ELF] phnum=%d, e_entry=%x, e_phoff=%x\n", phnum, ehdr->e_entry, ehdr->e_phoff);

    for (uint32_t i = 0; i < phnum; ++i) {
        phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff + i * ehdr->e_phentsize);

        printf("[ELF] seg[%d] type=%x vaddr=%x filesz=%x memsz=%x offset=%x flags=%x\n",
               i, phdr->p_type, phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz, phdr->p_offset, phdr->p_flags);

        if (phdr->p_type != PT_LOAD) {
            printf("[ELF] seg[%d] skipped (not PT_LOAD)\n", i);
            continue;
        }

        if (phdr->p_offset + phdr->p_filesz > size) {
            printf("[ELF] ERROR: seg[%d] exceeds image boundary (offset+filesz=%x > size=%x)\n",
                   i, phdr->p_offset + phdr->p_filesz, size);
            return 0;
        }

        uintptr_t aligned_load_vaddr = phdr->p_vaddr & ~0xFFF;
        uintptr_t aligned_load_vaddr_end = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFF;
        uint32_t load_size = aligned_load_vaddr_end - aligned_load_vaddr;

        printf("[ELF] seg[%d] aligned vaddr=[%x, %x), load_size=%x (%d pages)\n",
               i, aligned_load_vaddr, aligned_load_vaddr_end, load_size, load_size / 0x1000);

        void* load_paddr = elf_malloc(load_size);
        if (!load_paddr) {
            printf("[ELF] ERROR: elf_malloc failed for seg[%d], size=%x\n", i, load_size);
            return 0;
        }
        printf("[ELF] seg[%d] allocated paddr=%x\n", i, (uint32_t)load_paddr);

        uint32_t load_flag = elf_to_vmm_flags(phdr->p_flags);

        for (uintptr_t offset = 0; offset < load_size / 0x1000; ++offset) {
            elf_mmap((uintptr_t)load_paddr + offset * 0x1000,
                     aligned_load_vaddr + offset * 0x1000, load_flag);
        }

        printf("[ELF] seg[%d] memcpy: dst(vaddr)=%x src(image+%x) size=%x\n",
               i, phdr->p_vaddr, phdr->p_offset, phdr->p_filesz);

        memcpy((void*)phdr->p_vaddr,
               (void*)((uint32_t)elf_image + phdr->p_offset),
               phdr->p_filesz);

        if (phdr->p_filesz < phdr->p_memsz) {
            printf("[ELF] seg[%d] bss zero: vaddr=%x size=%x\n",
                   i, phdr->p_vaddr + phdr->p_filesz, phdr->p_memsz - phdr->p_filesz);
            memset((void*)((uint32_t)phdr->p_vaddr + phdr->p_filesz), 0,
                   phdr->p_memsz - phdr->p_filesz);
        }

        heap_addr = heap_addr < aligned_load_vaddr_end ? aligned_load_vaddr_end : heap_addr;
    }

    entry = ehdr->e_entry;
    printf("[ELF] done: entry=%x heap_addr=%x\n", entry, heap_addr);
    return heap_addr > 0 ? 1 : 0;
}