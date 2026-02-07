#ifndef HEAP_H
#define HEAP_H

#include "types.h"

/* Memory allocator - simple bump allocator for now */

#define HEAP_START   0x110000   /* Kernel heap starts at 1.1MB */
#define HEAP_SIZE    0x100000   /* 1MB heap */

/* Initialize heap */
void heap_init(void);

/* Allocate memory from heap */
void* malloc(size_t size);

/* Free memory (stub for now - bump allocator doesn't support free) */
void free(void* ptr);

/* Get heap statistics */
void heap_stats(void);

/* Get heap usage info */
size_t heap_used(void);
size_t heap_total(void);

/* Allocate zero-initialized memory */
void* calloc(size_t nmemb, size_t size);

#endif /* HEAP_H */
