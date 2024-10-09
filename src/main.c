#include "alloc.h"

void alloc_test();
void free_test();

int main(int argc, char** argv) {
    free_test();
}

void alloc_test() {
    int *x = allocate_memory(sizeof(int[100]));
    debug_print_heap();

    int *y = allocate_memory(sizeof(int[900]));
    debug_print_heap();

    int *z = allocate_memory(sizeof(int));
    debug_print_heap();

    int *w = allocate_memory(sizeof(int[10000]));
    debug_print_heap();
}

void free_test() {
    int *x = allocate_memory(sizeof(int));
    debug_print_heap();
    int *y = allocate_memory(sizeof(int));
    debug_print_heap();
    int *z = allocate_memory(sizeof(int));
    debug_print_heap();

    free_memory(y);
    debug_print_heap();
    free_memory(z);
    debug_print_heap();
    free_memory(x);
    debug_print_heap();
}
