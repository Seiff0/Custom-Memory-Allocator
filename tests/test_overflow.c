#include <stdint.h>
#include <stdio.h>
#include "mymalloc.h"
int main(void) {
    void *p = my_malloc(SIZE_MAX);
    if (p != NULL) { fprintf(stderr, "overflowing allocation succeeded\n"); return 1; }
    p = my_malloc(64);
    if (!p) return 2;
    if (my_realloc(p, SIZE_MAX) != NULL) { fprintf(stderr, "overflowing reallocation succeeded\n"); return 3; }
    my_free(p);
    return 0;
}
