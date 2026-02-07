#include "heap.h"
#include "vga.h"

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
    /* Clear heap memory */
    for (size_t i = 0; i < HEAP_SIZE; i++) {
        heap.heap_start[i] = 0;
    }
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

void heap_stats(void) {
    const uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_BROWN;
    
    char buf[60];
    size_t pos = 0;
    
    buf[pos++] = 'H';
    buf[pos++] = 'e';
    buf[pos++] = 'a';
    buf[pos++] = 'p';
    buf[pos++] = ':';
    buf[pos++] = ' ';
    
    /* Used memory in KB */
    size_t used_kb = heap.used / 1024;
    if (used_kb >= 100) {
        buf[pos++] = '0' + (used_kb / 100);
        buf[pos++] = '0' + ((used_kb / 10) % 10);
        buf[pos++] = '0' + (used_kb % 10);
    } else if (used_kb >= 10) {
        buf[pos++] = '0' + (used_kb / 10);
        buf[pos++] = '0' + (used_kb % 10);
    } else {
        buf[pos++] = '0' + used_kb;
    }
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos++] = ' ';
    buf[pos++] = '/';
    buf[pos++] = ' ';
    
    /* Total heap size in KB */
    size_t total_kb = HEAP_SIZE / 1024;
    if (total_kb >= 100) {
        buf[pos++] = '0' + (total_kb / 100);
        buf[pos++] = '0' + ((total_kb / 10) % 10);
        buf[pos++] = '0' + (total_kb % 10);
    } else if (total_kb >= 10) {
        buf[pos++] = '0' + (total_kb / 10);
        buf[pos++] = '0' + (total_kb % 10);
    } else {
        buf[pos++] = '0' + total_kb;
    }
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos] = '\0';
    
    vga_write_at(buf, 5, 0, color);
}

size_t heap_used(void) {
    return heap.used;
}

size_t heap_total(void) {
    return heap.heap_size;
}
