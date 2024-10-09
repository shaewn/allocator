#include "alloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h> // for mmap, munmap

// This structure is intentionally sized as 24 bytes so that the 8 more bytes
// from the chunk header will put the user data on a 16 byte alignment.
typedef struct Segment_ {
    size_t size;
    struct Segment_ *next;
    size_t null_header;
} Segment;

/*
 * Chunk header/footer:
 * a single 64-bit integer partitioned as follows:
 * uppermost 60 bits contain the 60 MSBs of the chunk size (including header and
 * footer). the 4 LSBs are zero, as allocations are 16-byte aligned. lowermost
 * 4 bits: top 3 unused, lowest = !is_free(this_chunk)
 */

void allocator_init(Allocator *allocator) {
    allocator->head = NULL;
}

void allocator_deinit(Allocator *allocator) {
    Segment *segment = allocator->head;

    while (segment) {
        Segment *next = segment->next;

        munmap(segment, segment->size);

        segment = next;
    }
}

static void new_segment(Allocator *allocator, size_t seg_size) {
    Segment *segment = mmap(NULL, seg_size, PROT_READ | PROT_WRITE,
                                   MAP_ANON | MAP_PRIVATE, -1, 0);
    segment->size = seg_size;
    segment->next = allocator->head;
    // segment->null_header = 0;
    // ^^^ automatically nulled out by mmap b/c MAP_ANON
    allocator->head = segment;
}

// pre: size is 16-byte aligned.
static void *allocate_in_new_segment(Allocator *allocator, size_t size) {
    size_t chunk_size = size + 2 * sizeof(size_t);
    size_t data_size = sizeof(Segment) + chunk_size;
    size_t total_size =
        data_size +
        sizeof(size_t); // make sure we have space to store a null chunk
    size_t seg_size = total_size + 4095 & ~4095;
    new_segment(allocator, seg_size);
    size_t *chunk_header = (void *)(allocator->head + 1);
    *chunk_header = chunk_size | 1; // or with 1 for in use.

    void *userdata = chunk_header + 1;

    size_t *chunk_footer = (void *)chunk_header + chunk_size - sizeof(size_t);
    *chunk_footer = chunk_size;

    if (seg_size - total_size > 2 * sizeof(size_t)) {
        chunk_size = seg_size - total_size;
        assert((chunk_size & 15) == 0);
        chunk_header = chunk_footer + 1;
        *chunk_header = chunk_size; // lsb is 0 for NOT in use.
        chunk_footer = (void *)chunk_header + chunk_size - sizeof(size_t);
        *chunk_footer = chunk_size;
    }

    return userdata;
}

// pre: size is 16-byte aligned.
// return: the first free chunk having at least size bytes of userdata space.
// NULL if no such chunk exists.
// if head is NULL, returns NULL
static void *find_chunk_of_min_size(Allocator allocator, size_t size) {
    Segment *segment = allocator.head;

    while (segment) {
        size_t *chunk_header = (void *)(segment + 1);

        while (*chunk_header) {
            size_t chunk_size = *chunk_header & ~1;

            if (!(*chunk_header & 1) &&
                size + 2 * sizeof(size_t) <=
                    chunk_size) { // not in use and has space
                return chunk_header;
            }

            chunk_header = (void *)chunk_header + chunk_size;
        }

        segment = segment->next;
    }

    return NULL;
}

void debug_print_heap(Allocator allocator) {
    Segment *segment = allocator.head;

    fputs("--------------------\n", stderr);

    while (segment) {
        fprintf(stderr, "SEGMENT %p\n", segment);

        size_t *chunk_header = (void *)(segment + 1);

        while (*chunk_header) {
            size_t chunk_size = *chunk_header & ~1;
            fprintf(stderr, "\tChunk of size %zu%s\n", chunk_size,
                    *chunk_header & 1 ? "" : " (free)");
            chunk_header = (void *)chunk_header + chunk_size;
        }

        segment = segment->next;
    }

    fputs("--------------------\n", stderr);
}

void *allocate_memory(Allocator *allocator, size_t size) {
    size = size + 15 & ~15;

    void *chunk = find_chunk_of_min_size(*allocator, size);

    if (!chunk) {
        return allocate_in_new_segment(allocator, size);
    }

    size_t *chunk_header = chunk;
    size_t chunk_size = *chunk_header;
    size_t excess = chunk_size - 2 * sizeof(size_t) - size;
    assert((excess & 15) == 0);

    if (excess > 2 * sizeof(size_t)) {
        // split the chunk in two.
        size_t new_size = chunk_size - excess;
        *chunk_header = new_size;
        size_t *chunk_footer = (void *)chunk_header + new_size - sizeof(size_t);
        *chunk_footer = new_size;

        size_t *new_header = chunk_footer + 1;
        *new_header = excess; // lsb is 0 for NOT in use
        size_t *new_footer = (void *)chunk_footer + excess;
        *new_footer = excess;
    }

    *chunk_header |= 1;

    return chunk_header + 1;
}

void consolidate_rightward(size_t *chunk_header) {
    size_t in_use = *chunk_header & 1;
    size_t chunk_size = *chunk_header ^ in_use;
    size_t *next_header = (void *)chunk_header + chunk_size;

    while (*next_header && (*next_header & 1) == 0) {
        chunk_size += *next_header;
        next_header = (void *)next_header + *next_header;
    }

    *chunk_header = chunk_size | in_use;

    size_t *chunk_footer = (void *)chunk_header + chunk_size - sizeof(size_t);
    *chunk_footer = chunk_size;
}

size_t *consolidate_leftward(size_t *chunk_header) {
    size_t in_use = *chunk_header & 1;
    size_t chunk_size = *chunk_header ^ in_use;

    size_t *new_header = chunk_header;

    size_t *prev_footer = chunk_header - 1;
    size_t *chunk_footer = (void *)prev_footer + chunk_size;
    while (*prev_footer) {
        size_t *prev_header =
            (void *)prev_footer - *prev_footer + sizeof(size_t);

        if (*prev_header & 1) {
            break;
        }

        chunk_size += *prev_footer;
        new_header = prev_header;
        prev_footer = prev_header - 1;
    }

    *new_header = chunk_size | in_use;
    *chunk_footer = chunk_size;

    return new_header;
}

void free_chunk(Allocator *allocator, size_t *chunk_header) {
    *chunk_header &= ~1;
    consolidate_rightward(chunk_header);
    chunk_header = consolidate_leftward(chunk_header);
    size_t *prev_footer = chunk_header - 1;
    size_t *next_header = (void *)chunk_header + *chunk_header;

    // We've reclaimed the entire segment, so remove it from the segment list
    // and unmap it.
    if (!*prev_footer && !*next_header) {
        Segment *segment = (void *)chunk_header - sizeof(struct Segment_);

        Segment **node = &allocator->head;

        while (*node != segment) {
            node = &(*node)->next;
        }

        *node = segment->next;

        munmap(segment, segment->size);
    }
}

void free_memory(Allocator *allocator, void *ptr) {
    if (!ptr) {
        return;
    }

    size_t *chunk_header = ptr - sizeof(size_t);
    free_chunk(allocator, chunk_header);
}

void *reallocate_memory(Allocator *allocator, void *ptr, size_t new_size) {
    if (!ptr) {
        return allocate_memory(allocator, new_size);
    }

    size_t data_size = new_size + 15 & ~15;
    size_t new_chunk_size = data_size + 2 * sizeof(size_t);
    size_t *chunk_header = ptr - sizeof(size_t);
    size_t old_chunk_size = (*chunk_header & ~1);

    size_t excess;

    if (new_chunk_size == old_chunk_size) {
        return ptr;
    } else if (new_chunk_size > old_chunk_size) {
        // consolidate memory rightward.
        // chunk_header stays in place during this operation.
        size_t accumulated_size = old_chunk_size;

        size_t *next_header = (void *)chunk_header + accumulated_size;

        while (accumulated_size < new_chunk_size && *next_header &&
               (*next_header & 1) == 0) {
            accumulated_size += *next_header;
            next_header = (void *)next_header + *next_header;
        }

        size_t *chunk_footer =
            (void *)chunk_header + accumulated_size - sizeof(size_t);
        *chunk_header = accumulated_size | 1;
        *chunk_footer = accumulated_size;

        if (accumulated_size < new_chunk_size) {
            free_chunk(allocator, chunk_header);
            return allocate_memory(allocator, new_size);
        }

        excess = accumulated_size - new_chunk_size;
    } else {
        // new_chunk_size < old_chunk_size
        excess = old_chunk_size - new_chunk_size;
    }

    // either new_chunk_size < old_chunk_size OR we successfully consolidated
    // rightward.

    if (excess > 2 * sizeof(size_t)) {
        *chunk_header = new_chunk_size | 1;
        size_t *chunk_footer =
            (void *)chunk_header + new_chunk_size - sizeof(size_t);
        *chunk_footer = new_chunk_size;

        size_t *new_header = (void *)chunk_header + new_chunk_size;
        *new_header = excess;

        size_t *new_footer = (void *)chunk_footer + new_chunk_size;
        *new_footer = excess;
    }

    return chunk_header + 1;
}
