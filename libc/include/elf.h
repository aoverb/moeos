#ifndef _ELF_H
#define _ELF_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

int verify_elf(void* elf_file, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
