/* Handbook 7 - the counter that lies (verbatim). Build with -O0 (the
 * Makefile does) or the optimiser may merge the increments. */
#include <stdio.h>
#include <pthread.h>

long count = 0;

void *worker(void *arg)
{
    (void)arg;
    for (long i = 0; i < 100000; i++)
        count++;

    return NULL;
}

int main(void)
{
    pthread_t a, b;

    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("Final   = %ld\n", count);
    printf("Expected= %d\n", 200000);

    return 0;
}
