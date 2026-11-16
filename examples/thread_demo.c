/* Handbook 6.1 - create one thread (verbatim). */
#include <stdio.h>
#include <pthread.h>

void *worker(void *arg)
{
    (void)arg;
    printf("Worker thread is running.\n");
    return NULL;
}

int main(void)
{
    pthread_t t;

    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    pthread_join(t, NULL);

    printf("Main: worker finished.\n");
    return 0;
}
