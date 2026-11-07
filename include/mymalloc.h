#ifndef MYMALLOC_H
#define MYMALLOC_H

#include <stddef.h> // For size_t

/* 
 * Enumeration for allocation strategies.
 * We only define it here (NOT in mymalloc.c).
 */
typedef enum {
    FIRST_FIT = 0,
    NEXT_FIT,
    BEST_FIT
} alloc_strategy_t;

/* 
 * Function prototypes:
 * We declare them here, implement them in mymalloc.c
 */
void set_allocation_strategy(alloc_strategy_t strategy);

void* my_malloc(size_t size);
void  my_free(void* ptr);
void* my_calloc(size_t nmemb, size_t size);
void* my_realloc(void* ptr, size_t size);
void print_memory_layout(void);

#endif // MYMALLOC_H
