/*
 * advanced_test_mymalloc.c
 *
 * Advanced test program for the custom memory allocator.
 *
 * This test exercises edge cases, fragmentation/coalescing,
 * randomized stress tests, and multi-threaded operations.
 *
 * Make sure that "mymalloc.h" (and its associated "mymalloc.c") is
 * available in the same directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "mymalloc.h"   // Ensure that this header declares:
                        //   - void* my_malloc(size_t size);
                        //   - void* my_calloc(size_t nmemb, size_t size);
                        //   - void* my_realloc(void* ptr, size_t size);
                        //   - void  my_free(void* ptr);
                        //   - void  set_allocation_strategy(alloc_strategy_t strategy);
                        //   - void  print_memory_layout();
                        //   - enum alloc_strategy_t { FIRST_FIT, NEXT_FIT, BEST_FIT };

static atomic_int failures = 0;

static unsigned int next_random(unsigned int *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

#define NUM_POINTERS    1000
#define NUM_ITERATIONS  10000
#define NUM_THREADS     4

/* 
 * Block structure used by the stress tests to track allocated memory.
 * For each allocated block, we store:
 *   - ptr: the pointer returned by my_malloc/my_realloc,
 *   - size: the requested size,
 *   - pattern: a random byte used to fill the block.
 */
typedef struct {
    void* ptr;
    size_t size;
    unsigned char pattern;
} Block;

/* 
 * Fills the memory area pointed to by ptr with the given byte (pattern).
 */
static void fill_pattern(void* ptr, size_t size, unsigned char pattern) {
    memset(ptr, pattern, size);
}

/*
 * Verifies that every byte in the memory area pointed to by ptr is equal
 * to the expected pattern. Returns 1 if the pattern is intact, 0 otherwise.
 */
static int verify_pattern(void* ptr, size_t size, unsigned char pattern) {
    unsigned char *p = (unsigned char *)ptr;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != pattern) {
            return 0; // Mismatch found
        }
    }
    return 1;
}

/*
 * Single-threaded stress test.
 *
 * This test creates an array of Block records, and for many iterations
 * randomly chooses an action:
 *   - Allocate a new block (if not already allocated),
 *   - Free an allocated block,
 *   - Reallocate an existing block to a new size.
 *
 * For each allocation, a random byte pattern is written into the block.
 * Before freeing or reallocating, the test verifies that the pattern is intact.
 */
void stress_test(unsigned int seed) {
    Block blocks[NUM_POINTERS];
    for (int i = 0; i < NUM_POINTERS; i++) {
        blocks[i].ptr = NULL;
        blocks[i].size = 0;
        blocks[i].pattern = 0;
    }
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        int index = next_random(&seed) % NUM_POINTERS;
        int action = next_random(&seed) % 3; // 0: allocate, 1: free, 2: realloc
        
        if (action == 0) { // Allocate
            if (blocks[index].ptr != NULL)
                continue;  // Already allocated
            size_t size = (next_random(&seed) % 1024) + 1; // Size between 1 and 1024 bytes
            void *ptr = my_malloc(size);
            if (!ptr)
                continue;  // Allocation failure; try later
            blocks[index].ptr = ptr;
            blocks[index].size = size;
            blocks[index].pattern = (unsigned char)(next_random(&seed) % 256);
            fill_pattern(ptr, size, blocks[index].pattern);
        }
        else if (action == 1) { // Free
            if (blocks[index].ptr != NULL) {
                if (!verify_pattern(blocks[index].ptr, blocks[index].size, blocks[index].pattern)) {
                    fprintf(stderr, "Data corruption detected before free at index %d\n", index);
                    atomic_fetch_add(&failures, 1);
                }
                my_free(blocks[index].ptr);
                blocks[index].ptr = NULL;
                blocks[index].size = 0;
                blocks[index].pattern = 0;
            }
        }
        else { // Reallocate
            if (blocks[index].ptr == NULL) {
                // If not allocated, behave like an allocation
                size_t size = (next_random(&seed) % 1024) + 1;
                void *ptr = my_malloc(size);
                if (!ptr)
                    continue;
                blocks[index].ptr = ptr;
                blocks[index].size = size;
                blocks[index].pattern = (unsigned char)(next_random(&seed) % 256);
                fill_pattern(ptr, size, blocks[index].pattern);
            } else {
                if (!verify_pattern(blocks[index].ptr, blocks[index].size, blocks[index].pattern)) {
                    fprintf(stderr, "Data corruption detected before realloc at index %d\n", index);
                    atomic_fetch_add(&failures, 1);
                }
                size_t new_size = (next_random(&seed) % 2048) + 1; // New size between 1 and 2048 bytes
                void *new_ptr = my_realloc(blocks[index].ptr, new_size);
                if (!new_ptr)
                    continue; // Realloc failed; keep old block
                size_t preserved = new_size < blocks[index].size ? new_size : blocks[index].size;
                if (!verify_pattern(new_ptr, preserved, blocks[index].pattern)) {
                    fprintf(stderr, "Data corruption detected after realloc at index %d\n", index);
                    atomic_fetch_add(&failures, 1);
                }
                // If expanded, fill the new portion with the same pattern.
                if (new_size > blocks[index].size) {
                    unsigned char *p = (unsigned char*)new_ptr;
                    memset(p + blocks[index].size, blocks[index].pattern, new_size - blocks[index].size);
                }
                blocks[index].ptr = new_ptr;
                blocks[index].size = new_size;
            }
        }
    }
    
    // Free any blocks still allocated.
    for (int i = 0; i < NUM_POINTERS; i++) {
        if (blocks[i].ptr != NULL) {
            if (!verify_pattern(blocks[i].ptr, blocks[i].size, blocks[i].pattern)) {
                fprintf(stderr, "Data corruption detected during cleanup at index %d\n", i);
                atomic_fetch_add(&failures, 1);
            }
            my_free(blocks[i].ptr);
            blocks[i].ptr = NULL;
        }
    }
}

/*
 * Edge Cases Test:
 *
 * This function verifies the following conditions:
 *   - my_malloc(0) should return NULL.
 *   - my_realloc(NULL, size) should behave like my_malloc(size).
 *   - my_realloc(ptr, 0) should free ptr and return NULL.
 *   - my_free(NULL) should be a no-op.
 */
void edge_cases_test() {
    printf("\n=== Edge Cases Test ===\n");
    
    void *ptr = my_malloc(0);
    if (ptr != NULL) {
        printf("Error: my_malloc(0) should return NULL.\n");
        atomic_fetch_add(&failures, 1);
    } else {
        printf("my_malloc(0) returned NULL as expected.\n");
    }
    
    size_t size = 128;
    void *ptr2 = my_realloc(NULL, size);
    if (ptr2 == NULL) {
        printf("Error: my_realloc(NULL, %lu) returned NULL.\n", (unsigned long)size);
        atomic_fetch_add(&failures, 1);
    } else {
        printf("my_realloc(NULL, %lu) allocated memory as expected.\n", (unsigned long)size);
    }
    my_free(ptr2);
    
    void *ptr3 = my_malloc(64);
    if (!ptr3) {
        printf("Error: my_malloc failed in edge case test.\n");
        atomic_fetch_add(&failures, 1);
    }
    void *ptr3_realloc = my_realloc(ptr3, 0);
    if (ptr3_realloc != NULL) {
        printf("Error: my_realloc(ptr, 0) should return NULL after freeing.\n");
        atomic_fetch_add(&failures, 1);
    } else {
        printf("my_realloc(ptr, 0) returned NULL as expected.\n");
    }
    
    my_free(NULL);
    printf("my_free(NULL) executed without error.\n");
}

/*
 * Fragmentation Test:
 *
 * Allocates five blocks of different sizes, then frees them in an order that
 * should force the allocator to coalesce adjacent free blocks. The memory layout
 * is printed after each free.
 */
void fragmentation_test() {
    printf("\n=== Fragmentation Test ===\n");
    void *blocks[5];
    size_t sizes[5] = {128, 256, 128, 256, 128};
    
    for (int i = 0; i < 5; i++) {
        blocks[i] = my_malloc(sizes[i]);
        if (!blocks[i]) {
            printf("Allocation failed for block %d\n", i);
            atomic_fetch_add(&failures, 1);
        }
    }
    print_memory_layout();
    
    printf("Freeing block 1 (256 bytes)\n");
    my_free(blocks[1]);
    print_memory_layout();
    
    printf("Freeing block 2 (128 bytes)\n");
    my_free(blocks[2]);
    print_memory_layout();
    
    printf("Freeing block 3 (256 bytes)\n");
    my_free(blocks[3]);
    print_memory_layout();
    
    printf("Freeing block 0 (128 bytes)\n");
    my_free(blocks[0]);
    printf("Freeing block 4 (128 bytes)\n");
    my_free(blocks[4]);
    print_memory_layout();
}

/*
 * Thread function for multi-threaded stress testing.
 *
 * Each thread runs a reproducible, independent random sequence.
 */
void *thread_func(void *arg) {
    int thread_id = *((int*)arg);
    printf("Thread %d starting stress test...\n", thread_id);
    stress_test(12345u + (unsigned int)thread_id);
    printf("Thread %d finished stress test.\n", thread_id);
    return NULL;
}

/*
 * Multi-threaded Stress Test:
 *
 * Spawns NUM_THREADS threads, each executing the stress_test() function.
 * This helps verify that the allocator’s locking (via pthread_mutex_t) is working.
 */
void multi_threaded_stress_test() {
    printf("\n=== Multi-threaded Stress Test ===\n");
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    int started[NUM_THREADS] = {0};
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_func, &thread_ids[i]) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i);
            atomic_fetch_add(&failures, 1);
        } else {
            started[i] = 1;
        }
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        if (started[i] && pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error joining thread %d\n", i);
            atomic_fetch_add(&failures, 1);
        }
    }
    printf("Multi-threaded stress test completed.\n");
}

/*
 * Main: Runs all the test suites.
 */
int main(void) {
    
    printf("=== Advanced Test Program for Custom Allocator ===\n");
    
    // Choose an allocation strategy (FIRST_FIT, NEXT_FIT, or BEST_FIT).
    set_allocation_strategy(BEST_FIT);
    
    // Run Edge Cases Test.
    edge_cases_test();
    
    // Run Fragmentation Test.
    fragmentation_test();
    
    // Run Single-threaded Stress Test.
    printf("\n=== Single-threaded Stress Test ===\n");
    stress_test(12345u);
    printf("Single-threaded stress test completed.\n");
    print_memory_layout();
    
    // Run Multi-threaded Stress Test.
    multi_threaded_stress_test();
    
    // Final memory layout.
    print_memory_layout();
    
    printf("\n=== All tests completed ===\n");

    if (atomic_load(&failures)) {
        fprintf(stderr, "%d test failure(s)\n", atomic_load(&failures));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
