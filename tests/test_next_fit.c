#include <stdio.h>
#include <stdint.h>
#include "mymalloc.h"
int main(void) {
    void *a = my_malloc(64), *b = my_malloc(64), *guard = my_malloc(64);
    if (!a || !b || !guard) return 2;
    set_allocation_strategy(NEXT_FIT);
    my_free(b);
    if (my_malloc(64) != b) return 3;
    my_free(b);
    my_free(a);
    void *x = my_malloc(64);
    set_allocation_strategy(FIRST_FIT);
    void *y = my_malloc(128);
    if ((uintptr_t)x >= (uintptr_t)y && (uintptr_t)x < (uintptr_t)y + 128) { fprintf(stderr, "next-fit reused an allocated block\n"); return 1; }
    my_free(x); my_free(y); my_free(guard);
    return 0;
}

