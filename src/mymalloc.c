#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include "mymalloc.h"

#ifdef _WIN32  // or #ifdef __MINGW32__
#include <windows.h>

/* 
 * On Windows, we can't use sbrk in a portable way, so we simulate it with 
 * VirtualAlloc. The custom 'my_sbrk' maintains our own "heap" region.
 */
static char *heap = NULL;    // Base of our custom heap
static size_t heap_size = 0; // How many bytes have been 'allocated' so far

void *my_sbrk(ptrdiff_t increment) {
    // Initialize our fake "heap" if not done yet
    if (!heap) {
        // Arbitrary large size for demonstration (16MB)
        size_t initial_size = 16 * 1024 * 1024;
        heap = (char *)VirtualAlloc(NULL, initial_size, 
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!heap) {
            fprintf(stderr, "[ERROR] VirtualAlloc failed to initialize heap.\n");
            return (void *)-1; // Allocation failed
        }
        heap_size = 0; // Start with 0 bytes used
    }

    // Just return the current 'break' if increment is 0
    if (increment == 0) {
        return (void *)(heap + heap_size);
    }

    // Check for out-of-bounds
    if (heap_size + increment > 16 * 1024 * 1024) {
        fprintf(stderr, "[ERROR] Heap out of space.\n");
        return (void *)-1; // Out of space
    }

    // Move our 'break' pointer
    void *old_break = (void *)(heap + heap_size);
    heap_size += increment;
    return old_break;
}

#else
/*
 * On POSIX/Linux systems, we can just use the real sbrk directly.
 */
#include <unistd.h>
static inline void *my_sbrk(ptrdiff_t increment) {
    return sbrk(increment);
}
#endif

/*Block metadata structure for tracking each block's size, free status, and links.*/
typedef struct block_meta {
    size_t size;               // Size of the user data
    struct block_meta *next;   // Next block in the list
    struct block_meta *prev;   // Previous block in the list
    int free;                  // 1 if this block is free, 0 otherwise
} block_meta_t;

/*Global variables for memory management.*/
static block_meta_t *g_head = NULL;               // Head of the doubly linked list of blocks
static block_meta_t *g_next_fit_cursor = NULL;    // Cursor used only for NEXT_FIT
static alloc_strategy_t g_alloc_strategy = FIRST_FIT;

/*
 * A mutex to ensure thread safety for all memory operations.
 * The public API locks/unlocks this; internal helpers assume we are already locked.
 */
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

/*Sets the global allocation strategy (FIRST_FIT, NEXT_FIT, or BEST_FIT).*/
void set_allocation_strategy(alloc_strategy_t strategy) {
    pthread_mutex_lock(&memory_lock);
    g_alloc_strategy = strategy;
    pthread_mutex_unlock(&memory_lock);
}

/*Round up the requested size to a multiple of 16 bytes for alignment.*/
static size_t align16(size_t size) {
    return (size + 15) & ~((size_t)0xF);
}

/*Given a pointer to user data, returns the pointer to the block's metadata.*/
static block_meta_t *get_block_meta(void *ptr) {
    return (block_meta_t *)((uint8_t *)ptr - sizeof(block_meta_t));
}

/*
 * Requests more space from the OS (via sbrk or Virtualalloc).
 * This function assumes the caller already holds the lock.
 */
static block_meta_t *request_space(block_meta_t *last, size_t size) {
    // Make one sbrk request for the block_meta plus the user data.
    void *request = my_sbrk(sizeof(block_meta_t) + size);
    if (request == (void *)-1) {
        fprintf(stderr, "[ERROR] Failed to allocate memory using sbrk.\n");
        return NULL; // sbrk failed
    }

    // The new block starts at the 'old break' returned by sbrk.
    block_meta_t *block = (block_meta_t *)request;
    block->size = size;
    block->free = 0;
    block->prev = last;
    block->next = NULL;

    // Link it in if there's a last block in the list
    if (last) {
        last->next = block;
    }

    return block;
}

/*
 * Split a block into two if it has enough extra space to form a second block.
 * This function assumes the caller already holds the lock.
 */
static void split_block(block_meta_t *block, size_t size) {
    // Only split if there's enough leftover space for a new block + alignment
    if (block->size >= size + sizeof(block_meta_t) + 16) {
        // Create a new block at the end of the requested size
        block_meta_t *new_block = 
            (block_meta_t *)((uint8_t *)block + sizeof(block_meta_t) + size);

        new_block->size = block->size - size - sizeof(block_meta_t);
        new_block->free = 1;
        new_block->prev = block;
        new_block->next = block->next;

        if (new_block->next) {
            new_block->next->prev = new_block;
        }

        // Adjust the original block
        block->size = size;
        block->next = new_block;
    }
}

/*
 * Merge (coalesce) consecutive free blocks around the given block.
 * This function assumes the caller already holds the lock.
 */
static void coalesce(block_meta_t *block) {
    // Merge with following blocks that are free
    while (block->next && block->next->free) {
        block->size += sizeof(block_meta_t) + block->next->size;
        block_meta_t *to_remove = block->next;
        if (g_next_fit_cursor == to_remove) g_next_fit_cursor = block;
        block->next = to_remove->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Merge with preceding blocks that are free
    while (block->prev && block->prev->free) {
        if (g_next_fit_cursor == block) g_next_fit_cursor = block->prev;
        block->prev->size += sizeof(block_meta_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
        block = block->prev; // Move backward in case we can coalesce further
    }
}

/*
 * Find the first free block that fits 'size' bytes (FIRST_FIT strategy).
 * This function assumes the caller already holds the lock.
 */
static block_meta_t *find_first_fit(size_t size) {
    block_meta_t *current = g_head;
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/*
 * Find the next free block starting from g_next_fit_cursor (NEXT_FIT strategy).
 * This function assumes the caller already holds the lock.
 */
static block_meta_t *find_next_fit(size_t size) {
    // If we don't have a cursor yet, start at the head
    if (!g_next_fit_cursor) {
        g_next_fit_cursor = g_head;
    }

    block_meta_t *start = g_next_fit_cursor;
    block_meta_t *current = start;

    // Loop until we come back to 'start'
    do {
        if (current->free && current->size >= size) {
            g_next_fit_cursor = current;
            return current;
        }
        current = current->next ? current->next : g_head;
    } while (current != start);
    
    return NULL; // Not found
}

/*
 * Find the smallest free block that fits 'size' bytes (BEST_FIT strategy).
 * This function assumes the caller already holds the lock.
 */
static block_meta_t *find_best_fit(size_t size) {
    block_meta_t *best = NULL;
    block_meta_t *current = g_head;

    while (current) {
        if (current->free && current->size >= size) {
            // If it's our first candidate or a smaller fit, choose it
            if (!best || current->size < best->size) {
                best = current;
            }
        }
        current = current->next;
    }
    return best;
}

/*
 * Dispatch function to choose the appropriate free-block finder based on g_alloc_strategy.
 * This function assumes the caller already holds the lock.
 */
static block_meta_t *find_free_block(size_t size) {
    switch (g_alloc_strategy) {
        case FIRST_FIT:
            return find_first_fit(size);
        case NEXT_FIT:
            return find_next_fit(size);
        case BEST_FIT:
            return find_best_fit(size);
        default:
            return find_first_fit(size);
    }
}

/*Public API: Allocates 'size' bytes and returns a pointer to the user space.*/
void* my_malloc(size_t size) {
    if (size == 0 || size > (size_t)PTRDIFF_MAX - sizeof(block_meta_t) - 15) {
        return NULL;
    }

    pthread_mutex_lock(&memory_lock);

    size = align16(size);  // Align the requested size to 16 bytes

    block_meta_t *block = NULL;

    // If we have no head yet, then request space for the very first block
    if (!g_head) {
        block = request_space(NULL, size);
        if (!block) {
            pthread_mutex_unlock(&memory_lock);
            return NULL; // sbrk (or VirtualAlloc) failed
        }
        g_head = block;
    }
    else {
        // Find a free block that fits
        block = find_free_block(size);
        if (!block) {
            // No suitable free block found; request more space
            block_meta_t *last = g_head;
            while (last->next) {
                last = last->next;
            }
            block = request_space(last, size);
            if (!block) {
                pthread_mutex_unlock(&memory_lock);
                return NULL; // Out of memory
            }
        }
        else {
            // Found a free block, mark it used and split if necessary
            block->free = 0;
            split_block(block, size);
        }
    }

    pthread_mutex_unlock(&memory_lock);

    // Return the address *after* the metadata
    return (uint8_t *)block + sizeof(block_meta_t);
}

/*
 * Public API: Frees a previously allocated block of memory.
 */
void my_free(void* ptr) {
    if (!ptr) return; // Free on NULL is a no-op by convention

    pthread_mutex_lock(&memory_lock);

    // Mark the block as free
    block_meta_t *block = get_block_meta(ptr);
    block->free = 1;

    // Attempt to coalesce with adjacent free blocks
    coalesce(block);

    pthread_mutex_unlock(&memory_lock);
}

/*Public API: Allocates an array of 'nmemb' elements each of 'size' bytes, and initializes all bytes to zero.*/
void* my_calloc(size_t nmemb, size_t size) {
    // Check for overflow
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        fprintf(stderr, "[ERROR] Overflow detected in calloc.\n");
        return NULL;
    }

    size_t total_size = nmemb * size;
    void* ptr = my_malloc(total_size);
    if (ptr) {
        // Set allocated memory to 0
        memset(ptr, 0, total_size);
    }
    return ptr;
}

/*
 * Public API: Resizes the memory block pointed to by 'ptr' to 'size' bytes.
 * If 'ptr' is NULL, this behaves like my_malloc.
 * If 'size' is 0, this behaves like free and returns NULL.
 */
void* my_realloc(void* ptr, size_t size) {
    // If ptr is NULL, act like malloc
    if (!ptr) {
        return my_malloc(size);
    }

    // If size == 0, free the block and return NULL
    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    if (size > (size_t)PTRDIFF_MAX - sizeof(block_meta_t) - 15) {
        return NULL;
    }

    pthread_mutex_lock(&memory_lock);

    block_meta_t *block = get_block_meta(ptr);

    size = align16(size);  // Align the new size

    // If the current block is already big enough, split if needed
    if (block->size >= size) {
        split_block(block, size);
        pthread_mutex_unlock(&memory_lock);
        return ptr;
    }

    // Otherwise, a new block is needed
    pthread_mutex_unlock(&memory_lock);
    void* new_ptr = my_malloc(size);
    if (!new_ptr) {
        return NULL;  // Allocation failed; original block remains valid
    }

    // Copy old data to new block
    pthread_mutex_lock(&memory_lock);
    memcpy(new_ptr, ptr, block->size);
    pthread_mutex_unlock(&memory_lock);

    my_free(ptr);    // Free the old block

    return new_ptr;
}

/*
 * Debugging tool: Print the current state of our memory layout.
 * Useful to understand how blocks are laid out and which are free/used.
 */
void print_memory_layout() {
    pthread_mutex_lock(&memory_lock);

    block_meta_t *current = g_head;
    printf("\nMemory Layout:\n");
    printf("-------------------------------------------------\n");

    size_t total = 0, free = 0;
    int blocks = 0, free_blocks = 0;

    while (current) {
        printf("Block %p | Size: %6zu | Free: %d\n", 
               (void*)current, current->size, current->free);
        total += current->size;
        blocks++;
        if (current->free) {
            free += current->size;
            free_blocks++;
        }
        current = current->next;
    }

    printf("-------------------------------------------------\n");
    printf("Total Blocks: %d | Used: %d | Free: %d\n", 
           blocks, blocks - free_blocks, free_blocks);
    printf("Total Memory: %zu bytes | Free Memory: %zu bytes\n", total, free);
    pthread_mutex_unlock(&memory_lock);
}


/* 
Single Locking Strategy:
Each public API function (my_malloc, my_free, my_calloc, my_realloc) acquires the mutex lock once at the beginning (or strategically around critical code) and releases it before returning. Internal helper functions (like request_space, split_block, coalesce, find_xxx_fit) assume the lock is already held, so they do not lock/unlock again. This avoids double-locking issues.

Simplified request_space:
We do a single call to my_sbrk(sizeof(block_meta_t) + size) and cast that pointer to a block_meta_t *. This is typically clearer than getting the old break first and then calling sbrk again, which can be done but is more prone to confusion.

Added Descriptive Comments:
More comments are included to clarify each function’s purpose, parameters, and assumptions about locks.

Alignment:
The align16(size) ensures that user data is 16-byte aligned, which can help with performance and may be required by certain architectures or instructions.

Thread Safety:
Ensuring that the entire block allocation, freeing, and coalescing processes are done under a single lock is crucial for correctness in multi-threaded environments.

Maintainable Structure:
The code is now more obviously divided into “Public API” vs. “Internal Helper” sections, making it simpler to maintain and troubleshoot.


Thread Safety: 
Memory Overflow Check: 
Added overflow protection for multiplication in my_calloc.

Custom Debugging Tool: 
Added a print_memory_layout() function to monitor the memory layout for debugging purposes.

Error Handling:
Added detailed error messages in my_sbrk for VirtualAlloc failures and memory exhaustion.
Added an error log for overflow detection in my_calloc.

Iterative Coalescing:
Replaced recursive coalesce calls with an iterative approach for better performance and stack safety.

Thread Safety:
Added pthread_mutex_t and used it to lock critical sections in memory allocation and deallocation functions.
Wrapped the find_free_block logic with a mutex to prevent race conditions during block searches.

Enhanced Debugging:
Updated print_memory_layout to include statistics like total memory, free memory, and block counts for better insight
*/