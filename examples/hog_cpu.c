/* Test helper for M2's runlimit/limit: burns CPU for N seconds (default: forever). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
    double seconds = argc > 1 ? atof(argv[1]) : 1e9;
    clock_t start = clock();
    volatile unsigned long x = 0;
    while ((double)(clock() - start) / CLOCKS_PER_SEC < seconds)
        for (int i = 0; i < 1000000; i++)
            x += (unsigned long)i;
    printf("hog_cpu: finished %.1f s of CPU\n", seconds);
    return 0;
}
