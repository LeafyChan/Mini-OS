#include "minios.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STRESS_THREADS 4
#define STRESS_ITEMS   5000

static job_queue_t q;
static atomic_int seen[STRESS_THREADS * STRESS_ITEMS];

static void *producer(void *arg)
{
    int base = (int)(long)arg * STRESS_ITEMS;
    for (int i = 0; i < STRESS_ITEMS; i++) {
        job_t j = { .id = base + i, .type = JOB_SUM };
        queue_push(&q, j);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        job_t j;
        queue_pop(&q, &j);
        if (j.type == JOB_STOP)
            return NULL;
        atomic_fetch_add(&seen[j.id], 1);
    }
}

static int stress(void)
{
    pthread_t p[STRESS_THREADS], c[STRESS_THREADS];
    queue_init(&q);
    for (long i = 0; i < STRESS_THREADS; i++) {
        pthread_create(&p[i], NULL, producer, (void *)i);
        pthread_create(&c[i], NULL, consumer, NULL);
    }
    for (int i = 0; i < STRESS_THREADS; i++)
        pthread_join(p[i], NULL);
    for (int i = 0; i < STRESS_THREADS; i++) {
        job_t stop = { .type = JOB_STOP };
        queue_push(&q, stop);
    }
    for (int i = 0; i < STRESS_THREADS; i++)
        pthread_join(c[i], NULL);

    int bad = 0;
    for (int i = 0; i < STRESS_THREADS * STRESS_ITEMS; i++)
        bad += atomic_load(&seen[i]) != 1;
    long pushed = atomic_load(&q.total_pushed), popped = atomic_load(&q.total_popped);
    queue_destroy(&q);

    printf("stress: %d items, %d delivered != once, pushed=%ld popped=%ld -> %s\n",
           STRESS_THREADS * STRESS_ITEMS, bad, pushed, popped,
           bad == 0 && pushed == popped ? "PASS" : "FAIL");
    return bad == 0 && pushed == popped ? 0 : 1;
}

static int frames(void)
{
    int fd[2];
    if (pipe(fd) != 0)
        return 1;
    char big[600];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    send_result(fd[1], "one");
    send_result(fd[1], big);
    send_result(fd[1], "three");
    close(fd[1]);

    char buf[100];
    int a = receive_result(fd[0], buf, sizeof buf);
    int ok = a == 3 && strcmp(buf, "one") == 0;
    int b = receive_result(fd[0], buf, sizeof buf);
    ok = ok && b == 99 && strlen(buf) == 99;
    int c = receive_result(fd[0], buf, sizeof buf);
    ok = ok && c == 5 && strcmp(buf, "three") == 0;
    int d = receive_result(fd[0], buf, sizeof buf);
    ok = ok && d == IPC_EOF;
    close(fd[0]);

    printf("frames: 3 messages kept apart, long one cut to %d bytes, then eof -> %s\n", b, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--frames") == 0)
        return frames();

    if (argc >= 2 && strcmp(argv[1], "--stress") == 0)
        return stress();

    if (argc >= 3 && strcmp(argv[1], "--workers") == 0) {
        int n = atoi(argv[2]);
        int jobs = argc >= 4 ? atoi(argv[3]) : 16;
        return workers_run(n, jobs, JOB_PRIMES, 60000, false, NULL) == 0 ? 0 : 1;
    }

    fprintf(stderr, "usage: minios_test --workers N [jobs] | --stress | --frames\n");
    return 2;
}
