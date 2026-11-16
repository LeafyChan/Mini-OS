/* Handbook 6.2 - two workers, one shared variable (verbatim). */
#include <stdio.h>
#include <pthread.h>

int count = 0;

void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) {
        count++;
        printf("worker: count=%d\n", count);
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;

    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("Final count=%d\n", count);
    return 0;
}
