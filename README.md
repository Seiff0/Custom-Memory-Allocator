# Custom Memory Allocator

An educational C implementation of a memory allocator. It offers first-fit, next-fit and best-fit allocation strategies, block splitting and coalescing, and a mutex to protect allocator state.

## Project layout

```text
include/mymalloc.h              Public API and allocation strategies
src/mymalloc.c                  Allocator implementation
src/mymalloc_wrapper.c          Experimental standard allocation wrapper
tests/                          Regression and stress tests
examples/test_app_standard.c    Example using standard allocation functions
docs/                           Original project report and summary
Makefile                        Build and test commands
```

## Requirements

GCC, Make and pthreads. The implementation uses `sbrk` on Linux; on Windows it uses `VirtualAlloc` with a fixed 16 MiB region. For Windows, use a MinGW toolchain with pthread support and Make.

## Build and test

From this directory:

```sh
make test
```

This builds and runs the overflow and next-fit regression tests, then the deterministic single-threaded and multithreaded stress test. To remove build outputs, run `make clean`.

To build with compiler warnings treated as errors:

```sh
make clean
make test CFLAGS="-std=c11 -Wall -Wextra -Werror -pthread"
```

## Use the allocator

Include `mymalloc.h` and compile `src/mymalloc.c` with your application, adding `-Iinclude` and `-pthread`. For example:

```c
#include "mymalloc.h"

int main(void) {
    set_allocation_strategy(BEST_FIT);
    void *buffer = my_malloc(128);
    if (!buffer) return 1;
    my_free(buffer);
    return 0;
}
```

The API also provides `my_calloc`, `my_realloc` and `print_memory_layout`. A zero-size `my_malloc` returns `NULL`; `my_free(NULL)` does nothing.

## Limitations

This is a teaching project, not a production replacement for the system allocator. Linux `sbrk` can conflict with other users of the process break. The wrapper for replacing standard allocation functions is experimental and is not covered by `make test`; Linux execution and wrapper interposition have not been verified in the current environment. The Windows build and tests have been run with warnings treated as errors.
