// ===========================================================================
// allocator.cpp - std::mem_alloc / std::mem_free implementation
//
// extern "C" lives ONLY here, never in any header file.
// ===========================================================================

#include "allocator"

extern "C" {
#if defined(__is_libk)
void* kmalloc(size_t size);
void  kfree(void* ptr);
#else
void* malloc(size_t size);
void  free(void* ptr);
#endif
}

namespace std {

void* mem_alloc(size_t size) {
#if defined(__is_libk)
    return kmalloc(size);
#else
    return malloc(size);
#endif
}

void mem_free(void* ptr) {
#if defined(__is_libk)
    kfree(ptr);
#else
    free(ptr);
#endif
}

} // namespace std