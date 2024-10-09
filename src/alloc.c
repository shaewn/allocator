#include "alloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h> // for mmap, munmap

// This structure is intentionally sized as 24 bytes so that the 8 more bytes
// from the chunk header will put the user data on a 16 byte alignment.
struct segment {
    size_t size;
    struct segment *next;
    size_t null_header;
};

/*
 * Chunk header/footer:
 * a single 64-bit integer partitioned as follows:
 * uppermost 60 bits contain the 60 MSBs of the chunk size (including header and
 * footer). the 4 LSBs are zero, as allocations are 16-byte aligned. lowermost
 * 4 bits: top 3 unused, lowest = !is_free(this_chunk)
 */

static struct segment *head = NULL;

static void new_segment(size_t seg_size) {
    struct segment *segment = mmap(NULL, seg_size, PROT_READ | PROT_WRITE,
                                   MAP_ANON | MAP_PRIVATE, -1, 0);
    segment->size = seg_size;
    segment->next = head;
    // segment->null_header = 0;
    // ^^^ automatically nulled out by mmap b/c MAP_ANON
    head = segment;
}

// pre: size is 16-byte aligned.
static void *allocate_in_new_segment(size_t size) {
    size_t chunk_size = size + 2 * sizeof(size_t);
    size_t data_size = sizeof(struct segment) + chunk_size;
    size_t total_size =
        data_size +
        sizeof(size_t); // make sure we have space to store a null chunk
    size_t seg_size = total_size + 4095 & ~4095;
    new_segment(seg_size);
    size_t *chunk_header = (void *)(head + 1);
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
static void *find_chunk_of_min_size(size_t size) {
    struct segment *segment = head;

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

void debug_print_heap() {
    struct segment *segment = head;

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

void *allocate_memory(size_t size) {
    size = size + 15 & ~15;

    void *chunk = find_chunk_of_min_size(size);

    if (!chunk) {
        return allocate_in_new_segment(size);
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

void free_memory(void *ptr) {
    if (!ptr) {
        return;
    }

    size_t *chunk_header = ptr - sizeof(size_t);
    *chunk_header &= ~1;
    size_t *prev_footer = chunk_header - 1;
    size_t *new_header = chunk_header;

    while (*prev_footer) {
        size_t *prev_header =
            (void *)prev_footer - *prev_footer + sizeof(size_t);

        if (*prev_header & 1) {
            break;
        }

        new_header = prev_header;
        prev_footer = new_header - 1;
    }

    size_t *new_footer = (void *)chunk_header + *chunk_header - sizeof(size_t);
    size_t *next_header = new_footer + 1;
    while (*next_header) {
        if (*next_header & 1) {
            break;
        }

        next_header = (void *)next_header + *next_header;
        size_t *next_footer = (void *)next_header - sizeof(size_t);

        new_footer = next_footer;
    }

    *new_header = (void *)new_footer - (void *)new_header + sizeof(size_t);
    *new_footer = *new_header;

    // We've reclaimed the entire segment, so remove it from the segment list
    // and unmap it.
    if (!*prev_footer && !*next_header) {
        struct segment *segment = (void *)new_header - sizeof(struct segment);

        struct segment **node = &head;

        while (*node != segment) {
            node = &(*node)->next;
        }

        *node = segment->next;

        munmap(segment, segment->size);
    }
}

void *reallocate_memory(void *ptr, size_t new_size) {
    new_size = new_size + 15 & ~15;
    // first, if new_size is smaller than size, check if there'd be enough space
    // left over to split this chunk. if so, split the chunk to the right.
    // second, if new_size is greater than size, see how large we can make the
    // chunk by consolidating the chunks on the right side. If the amount of
    // space in the new (consolidated) chunk exceeds or matches new_size, then
    // we've successfully extended the allocation. If it doesn't, free and
    // allocate again. make an impl method consolidate_rightward(size_t
    // *chunk_header), consolidate_leftward(size_t *chunk_header),
    // free_chunk(size_t *chunk_header);
    return NULL;
}
