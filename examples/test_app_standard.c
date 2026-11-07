#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("Starting test_app using standard functions...\n");

    char *buf = malloc(100);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    strcpy(buf, "Hello from test_app using LD_PRELOAD!");
    printf("%s\n", buf);

    buf = realloc(buf, 200);
    strcat(buf, " More text added.");
    printf("%s\n", buf);

    char *array = calloc(10, sizeof(char));
    strcpy(array, "Test");
    printf("%s\n", array);

    free(buf);
    free(array);

    printf("test_app completed.\n");
    return 0;
}
