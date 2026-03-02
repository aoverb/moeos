#ifndef _ELF_H
#define _ELF_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

#define EI_NIDENT 16

#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3

#define EM_NONE  0
#define EM_386   3

#define PT_NULL 0
#define PT_LOAD 1

/* ELF Program Header Flags */
#define PF_X        (1 << 0)    /* 可执行 (Execute) */
#define PF_W        (1 << 1)    /* 可写 (Write) */
#define PF_R        (1 << 2)    /* 可读 (Read) */
#define PF_MASKOS   0x0ff00000  /* 操作系统特定保留位 */
#define PF_MASKPROC 0xf0000000  /* 处理器特定保留位 */

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

int verify_elf(void* elf_image, uint32_t size);
int construct_user_space_by_elf_image(void* elf_image, uint32_t size, uint32_t& entry, uint32_t &heap_addr);

#ifdef __cplusplus
}
#endif

#endif
