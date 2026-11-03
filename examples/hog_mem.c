/* Test helper for M2's runlimit: allocates and touches N MB (default 200). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    long mb = argc > 1 ? atol(argv[1]) : 200;
    for (long i = 0; i < mb; i++) {
        char *p = malloc(1024 * 1024);
        if (p == NULL) {
            printf("hog_mem: malloc failed after %ld MB (limit reached)\n", i);
            return 3;
        }
        memset(p, 0xAB, 1024 * 1024);
    }
    printf("hog_mem: allocated %ld MB\n", mb);
    return 0;
}
