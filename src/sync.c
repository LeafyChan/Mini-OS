#include "minios.h"

#include <errno.h>
#include <string.h>

static int sem_wait_retry(sem_t *s)
{
    while (sem_wait(s) != 0)
        if (errno != EINTR)
            return -1;
    return 0;
}

int queue_init(job_queue_t *q)
{
    memset(q, 0, sizeof *q);
    if (sem_init(&q->mutex, 0, 1) != 0)
        return -1;
    if (sem_init(&q->empty, 0, QUEUE_CAPACITY) != 0)
        return -1;
    if (sem_init(&q->full, 0, 0) != 0)
        return -1;
    return 0;
}

void queue_destroy(job_queue_t *q)
{
    sem_destroy(&q->mutex);
    sem_destroy(&q->empty);
    sem_destroy(&q->full);
}

int queue_push(job_queue_t *q, job_t job)
{
    atomic_fetch_add(&q->blocked_producers, 1);
    int rc = sem_wait_retry(&q->empty);
    atomic_fetch_sub(&q->blocked_producers, 1);
    if (rc != 0)
        return -1;

    sem_wait_retry(&q->mutex);
    q->items[q->in] = job;
    q->in = (q->in + 1) % QUEUE_CAPACITY;
    atomic_fetch_add(&q->count, 1);
    atomic_fetch_add(&q->total_pushed, 1);
    sem_post(&q->mutex);
    sem_post(&q->full);
    return 0;
}

int queue_snapshot(job_queue_t *q, bool occupied[QUEUE_CAPACITY], int ids[QUEUE_CAPACITY])
{
    sem_wait_retry(&q->mutex);
    int n = atomic_load(&q->count);
    for (int i = 0; i < QUEUE_CAPACITY; i++) {
        occupied[i] = false;
        ids[i] = 0;
    }
    for (int i = 0; i < n; i++) {
        int slot = (q->out + i) % QUEUE_CAPACITY;
        occupied[slot] = true;
        ids[slot] = q->items[slot].type == JOB_STOP ? -1 : q->items[slot].id;
    }
    sem_post(&q->mutex);
    return n;
}

int queue_pop(job_queue_t *q, job_t *job)
{
    atomic_fetch_add(&q->blocked_consumers, 1);
    int rc = sem_wait_retry(&q->full);
    atomic_fetch_sub(&q->blocked_consumers, 1);
    if (rc != 0)
        return -1;

    sem_wait_retry(&q->mutex);
    *job = q->items[q->out];
    q->out = (q->out + 1) % QUEUE_CAPACITY;
    atomic_fetch_sub(&q->count, 1);
    atomic_fetch_add(&q->total_popped, 1);
    sem_post(&q->mutex);
    sem_post(&q->empty);
    return 0;
}

typedef struct {
    long              iters;
    race_mode_t       mode;
    volatile long    *plain;
    atomic_long      *atom;
    sem_t            *sem;
    pthread_mutex_t  *mtx;
} race_arg_t;

static void *race_worker(void *p)
{
    race_arg_t *a = p;
    block_signals_in_thread();
    for (long i = 0; i < a->iters; i++) {
        switch (a->mode) {
        case RACE_UNSAFE:
            (*a->plain)++;
            break;
        case RACE_SEMAPHORE:
            sem_wait_retry(a->sem);
            (*a->plain)++;
            sem_post(a->sem);
            break;
        case RACE_MUTEX:
            pthread_mutex_lock(a->mtx);
            (*a->plain)++;
            pthread_mutex_unlock(a->mtx);
            break;
        case RACE_ATOMIC:
            atomic_fetch_add(a->atom, 1);
            break;
        }
    }
    return NULL;
}

long race_run(long iters, int threads, race_mode_t mode, double *ms)
{
    volatile long   plain = 0;
    atomic_long     atom  = 0;
    sem_t           sem;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_t       tid[64];
    race_arg_t      arg = { iters, mode, &plain, &atom, &sem, &mtx };

    if (threads < 1 || threads > 64)
        return -1;
    sem_init(&sem, 0, 1);

    double t0 = now_ms();
    int started = 0;
    for (; started < threads; started++)
        if (pthread_create(&tid[started], NULL, race_worker, &arg) != 0)
            break;
    for (int i = 0; i < started; i++)
        pthread_join(tid[i], NULL);
    if (ms)
        *ms = now_ms() - t0;

    sem_destroy(&sem);
    return mode == RACE_ATOMIC ? (long)atom : (long)plain;
}

cmd_status_t cmd_race(int argc, char **argv)
{
    long iters = 1000000, threads = 2, trials = 3;

    if ((argc > 1 && (!parse_long(argv[1], &iters)   || iters < 1 || iters > 100000000)) ||
        (argc > 2 && (!parse_long(argv[2], &threads) || threads < 2 || threads > 64)) ||
        (argc > 3 && (!parse_long(argv[3], &trials)  || trials < 1 || trials > 20))) {
        fprintf(stderr, "usage: race [iters] [threads 2..64] [trials 1..20]\n");
        return CMD_ERROR;
    }

    long expected = iters * threads;
    printf("%ld threads x %ld increments, expected %ld\n", threads, iters, expected);

    for (long t = 1; t <= trials; t++) {
        double ms;
        long got = race_run(iters, (int)threads, RACE_UNSAFE, &ms);
        printf("  unsafe    trial %ld: final = %ld  lost = %ld  %s\n", t, got, expected - got,
               got == expected ? "correct (lucky)" : "WRONG");
    }

    static const struct { const char *name; race_mode_t mode; } fixes[] = {
        { "semaphore", RACE_SEMAPHORE }, { "mutex", RACE_MUTEX }, { "atomic", RACE_ATOMIC }
    };
    for (size_t i = 0; i < sizeof fixes / sizeof fixes[0]; i++) {
        double ms;
        long got = race_run(iters, (int)threads, fixes[i].mode, &ms);
        printf("  %-9s final = %ld  %8.1f ms  %s\n", fixes[i].name, got, ms,
               got == expected ? "correct" : "WRONG");
    }
    return CMD_OK;
}
