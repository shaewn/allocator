#include "alloc.h"
#include <stdio.h>

Allocator allocator;

void alloc_test();
void free_test();
void realloc_test();

int main(int argc, char** argv) {
    allocator_init(&allocator);

    fputs("ALLOC TEST\n", stderr);
    alloc_test();
    
    allocator_deinit(&allocator);
    allocator_init(&allocator);

    fputs("\n\nFREE TEST\n", stderr);
    free_test();

    allocator_deinit(&allocator);
    allocator_init(&allocator);

    fputs("\n\nREALLOC TEST\n", stderr);
    realloc_test();

    allocator_deinit(&allocator);
}

void alloc_test() {
    int *x = allocate_memory(&allocator, sizeof(int[100]));
    debug_print_heap(allocator);

    int *y = allocate_memory(&allocator, sizeof(int[900]));
    debug_print_heap(allocator);

    int *z = allocate_memory(&allocator, sizeof(int));
    debug_print_heap(allocator);

    int *w = allocate_memory(&allocator, sizeof(int[10000]));
    debug_print_heap(allocator);
}

void free_test() {
    int *x = allocate_memory(&allocator, sizeof(int));
    debug_print_heap(allocator);
    int *y = allocate_memory(&allocator, sizeof(int));
    debug_print_heap(allocator);
    int *z = allocate_memory(&allocator, sizeof(int));
    debug_print_heap(allocator);

    free_memory(&allocator, y);
    debug_print_heap(allocator);
    free_memory(&allocator, z);
    debug_print_heap(allocator);
    free_memory(&allocator, x);
    debug_print_heap(allocator);
}

void realloc_test() {
    int size = 4;
    int *x = allocate_memory(&allocator, sizeof(int[size]));

    debug_print_heap(allocator);
    putchar('\n');

    while (size < 1 << 20) { // up to 4 mebibytes of data.
        size <<= 1;

        fprintf(stderr, "size: %d\n\n", size);
        x = reallocate_memory(&allocator, x, sizeof(int[size]));

        debug_print_heap(allocator);
        putchar('\n');
    }
}
