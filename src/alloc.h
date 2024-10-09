#ifndef _ALLOC_H_GUARD_
#define _ALLOC_H_GUARD_

#include <stddef.h>

typedef struct Allocator_ {
    struct Segment_ *head;
} Allocator;

// Any functions that take a Allocator* might modify the head segment.

void allocator_init(Allocator *allocator);
void allocator_deinit(Allocator *allocator);

void *allocate_memory(Allocator *allocator, size_t size);
void free_memory(Allocator *allocator, void *ptr);

// Unimplemented
void *reallocate_memory(Allocator *allocator, void *ptr, size_t new_size);

void debug_print_heap(Allocator allocator);

#endif // _ALLOC_H_GUARD_
