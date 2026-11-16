/* Handbook 7.3 - semaphore protection (verbatim). */
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

long count = 0;
sem_t mutex;

void *worker(void *arg)
{
    (void)arg;
    for (long i = 0; i < 100000; i++) {
        sem_wait(&mutex);

        count++;

        sem_post(&mutex);
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;

    sem_init(&mutex, 0, 1);

    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);

    printf("Final count = %ld\n", count);

    sem_destroy(&mutex);
    return 0;
}
