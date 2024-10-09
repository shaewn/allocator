#ifndef _ALLOC_H_GUARD_
#define _ALLOC_H_GUARD_

#include <stddef.h>

void *allocate_memory(size_t size);
void free_memory(void *ptr);

// Unimplemented
void *reallocate_memory(void *ptr, size_t new_size);

void debug_print_heap();

#endif // _ALLOC_H_GUARD_
