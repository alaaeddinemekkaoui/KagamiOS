#include "heap.h"
#include "include/serial.h"

/* Define NULL if not available */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Heap allocator state */
typedef struct {
    uint8_t* heap_start;
    uint8_t* heap_ptr;
    size_t heap_size;
    size_t used;
} HEAP_STATE;

static HEAP_STATE heap = {
    .heap_start = (uint8_t*)HEAP_START,
    .heap_ptr = (uint8_t*)HEAP_START,
    .heap_size = HEAP_SIZE,
    .used = 0
};

void heap_init(void) {
    /* Avoid touching unmapped memory early; just reset pointers */
    heap.heap_ptr = heap.heap_start;
    heap.used = 0;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    /* Check if we have enough space */
    if (heap.used + size > heap.heap_size) {
        return NULL;  /* Out of memory */
    }
    
    void* ptr = heap.heap_ptr;
    heap.heap_ptr += size;
    heap.used += size;
    
    return ptr;
}

void free(void* ptr) {
    /* Bump allocator doesn't support free - this is a stub */
    (void)ptr;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void* ptr = malloc(total_size);
    
    if (ptr) {
        /* Zero-initialize */
        uint8_t* b = (uint8_t*)ptr;
        for (size_t i = 0; i < total_size; i++) {
            b[i] = 0;
        }
    }
    
    return ptr;
}

static void append_uint_dec(char *buf, size_t *pos, size_t value) {
    char tmp[20];
    size_t n = 0;
    if (value == 0) {
        buf[(*pos)++] = '0';
        return;
    }
    while (value > 0 && n < sizeof(tmp)) {
        tmp[n++] = '0' + (value % 10);
        value /= 10;
    }
    while (n > 0) {
        buf[(*pos)++] = tmp[--n];
    }
}

void heap_stats(void) {
    char buf[80];
    size_t pos = 0;

    const char *prefix = "Heap: ";
    while (*prefix) {
        buf[pos++] = *prefix++;
    }

    /* Used memory in KB */
    size_t used_kb = heap.used / 1024;
    append_uint_dec(buf, &pos, used_kb);
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos++] = ' ';
    buf[pos++] = '/';
    buf[pos++] = ' ';

    /* Total heap size in KB */
    size_t total_kb = HEAP_SIZE / 1024;
    append_uint_dec(buf, &pos, total_kb);
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos++] = '\n';
    buf[pos] = '\0';

    serial_write(buf);
}

size_t heap_used(void) {
    return heap.used;
}

size_t heap_total(void) {
    return heap.heap_size;
}
