// mymalloc_wrapper.c
#include "mymalloc.h"

/*
 * These functions wrap your custom allocator so that calls to
 * malloc, free, calloc, and realloc in any program get redirected.
 */
void* malloc(size_t size) {
    return my_malloc(size);
}

void free(void* ptr) {
    my_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
    return my_calloc(nmemb, size);
}

void* realloc(void* ptr, size_t size) {
    return my_realloc(ptr, size);
}
