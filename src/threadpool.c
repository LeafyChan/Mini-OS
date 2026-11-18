#include "minios.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define POOL_LOG_MAX 256

typedef struct pool_worker {
    struct thread_pool *pool;
    pthread_t           tid;
    int                 index;
    bool                busy;
    int                 current_job;
    long                done;
    double              busy_ms;
} pool_worker_t;

struct thread_pool {
    pthread_mutex_t lock;
    pthread_cond_t  work_cv;
    pthread_cond_t  idle_cv;
    pthread_mutex_t resize_lock;

    job_t          *heap;
    size_t          size, cap;
    sched_policy_t  policy;
    uint64_t        next_seq;

    pool_worker_t  *workers[POOL_MAX_THREADS];
    int             nworkers;
    int             target;
    int             active;
    bool            paused;
    bool            stopping;

    long            submitted, completed;
    double          total_wait_ms, total_run_ms;
    int             order[POOL_LOG_MAX];
    size_t          order_len;
};

static bool before(const thread_pool_t *p, const job_t *a, const job_t *b)
{
    if (p->policy == POLICY_PRIORITY && a->priority != b->priority)
        return a->priority > b->priority;
    return a->seq < b->seq;
}

static void sift_up(thread_pool_t *p, size_t i)
{
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (!before(p, &p->heap[i], &p->heap[parent]))
            break;
        job_t t = p->heap[i];
        p->heap[i] = p->heap[parent];
        p->heap[parent] = t;
        i = parent;
    }
}

static void sift_down(thread_pool_t *p, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, best = i;
        if (l < p->size && before(p, &p->heap[l], &p->heap[best])) best = l;
        if (r < p->size && before(p, &p->heap[r], &p->heap[best])) best = r;
        if (best == i)
            return;
        job_t t = p->heap[i];
        p->heap[i] = p->heap[best];
        p->heap[best] = t;
        i = best;
    }
}

static job_t heap_pop(thread_pool_t *p)
{
    job_t top = p->heap[0];
    p->heap[0] = p->heap[--p->size];
    sift_down(p, 0);
    return top;
}

static void *pool_worker_main(void *arg)
{
    pool_worker_t *self = arg;
    thread_pool_t *p = self->pool;
    block_signals_in_thread();

    pthread_mutex_lock(&p->lock);
    for (;;) {
        while (self->index < p->target && !(p->stopping && p->size == 0) &&
               (p->size == 0 || (p->paused && !p->stopping)))
            pthread_cond_wait(&p->work_cv, &p->lock);

        if (self->index >= p->target || (p->stopping && p->size == 0))
            break;

        job_t job = heap_pop(p);
        self->busy = true;
        self->current_job = job.id;
        p->active++;
        double waited = now_ms() - job.submit_ms;
        pthread_mutex_unlock(&p->lock);

        char text[RESULT_MAX];
        double t0 = now_ms();
        job_execute(&job, text, sizeof text);
        double ran = now_ms() - t0;

        pthread_mutex_lock(&p->lock);
        self->busy = false;
        self->done++;
        self->busy_ms += ran;
        p->active--;
        p->completed++;
        p->total_wait_ms += waited;
        p->total_run_ms += ran;
        if (p->order_len < POOL_LOG_MAX)
            p->order[p->order_len++] = job.id;
        if (p->size == 0 && p->active == 0)
            pthread_cond_broadcast(&p->idle_cv);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

static int spawn(thread_pool_t *p, int index)
{
    pool_worker_t *w = calloc(1, sizeof *w);
    if (!w)
        return -1;
    w->pool = p;
    w->index = index;
    if (pthread_create(&w->tid, NULL, pool_worker_main, w) != 0) {
        free(w);
        return -1;
    }
    p->workers[index] = w;
    return 0;
}

thread_pool_t *pool_create(int nthreads, sched_policy_t policy)
{
    thread_pool_t *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    pthread_mutex_init(&p->lock, NULL);
    pthread_mutex_init(&p->resize_lock, NULL);
    pthread_cond_init(&p->work_cv, NULL);
    pthread_cond_init(&p->idle_cv, NULL);
    p->policy = policy;
    p->cap = 16;
    p->heap = malloc(p->cap * sizeof *p->heap);
    if (!p->heap) {
        free(p);
        return NULL;
    }
    if (pool_resize(p, nthreads) != 0) {
        pool_destroy(p);
        return NULL;
    }
    return p;
}

int pool_submit(thread_pool_t *p, job_t job)
{
    pthread_mutex_lock(&p->lock);
    if (p->stopping) {
        pthread_mutex_unlock(&p->lock);
        return -1;
    }
    if (p->size == p->cap) {
        job_t *bigger = realloc(p->heap, p->cap * 2 * sizeof *bigger);
        if (!bigger) {
            pthread_mutex_unlock(&p->lock);
            return -1;
        }
        p->heap = bigger;
        p->cap *= 2;
    }
    job.seq = p->next_seq++;
    job.submit_ms = now_ms();
    p->heap[p->size++] = job;
    sift_up(p, p->size - 1);
    p->submitted++;
    pthread_cond_signal(&p->work_cv);
    pthread_mutex_unlock(&p->lock);
    return 0;
}

int pool_resize(thread_pool_t *p, int n)
{
    if (n < 1 || n > POOL_MAX_THREADS)
        return -1;
    pthread_mutex_lock(&p->resize_lock);
    pthread_mutex_lock(&p->lock);
    int old = p->nworkers;
    p->target = n;
    int rc = 0;
    if (n > old) {
        for (int i = old; i < n; i++) {
            if (spawn(p, i) != 0) {
                p->target = i;
                rc = -1;
                break;
            }
            p->nworkers = i + 1;
        }
        pthread_mutex_unlock(&p->lock);
    } else if (n < old) {
        pthread_cond_broadcast(&p->work_cv);
        pthread_mutex_unlock(&p->lock);
        for (int i = n; i < old; i++) {
            pthread_join(p->workers[i]->tid, NULL);
            free(p->workers[i]);
            p->workers[i] = NULL;
        }
        pthread_mutex_lock(&p->lock);
        p->nworkers = n;
        pthread_mutex_unlock(&p->lock);
    } else {
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&p->resize_lock);
    return rc;
}

void pool_set_policy(thread_pool_t *p, sched_policy_t policy)
{
    pthread_mutex_lock(&p->lock);
    p->policy = policy;
    for (size_t i = p->size / 2 + 1; i-- > 0;)
        sift_down(p, i);
    pthread_mutex_unlock(&p->lock);
}

void pool_pause(thread_pool_t *p, bool paused)
{
    pthread_mutex_lock(&p->lock);
    p->paused = paused;
    if (!paused)
        pthread_cond_broadcast(&p->work_cv);
    pthread_mutex_unlock(&p->lock);
}

int pool_wait_idle(thread_pool_t *p)
{
    pthread_mutex_lock(&p->lock);
    while ((p->size > 0 || p->active > 0) && !minios_interrupted) {
        if (p->paused && p->active == 0) {
            pthread_mutex_unlock(&p->lock);
            return -1;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&p->idle_cv, &p->lock, &ts);
    }
    pthread_mutex_unlock(&p->lock);
    return 0;
}

size_t pool_completed_order(thread_pool_t *p, int *ids, size_t max)
{
    pthread_mutex_lock(&p->lock);
    size_t n = p->order_len < max ? p->order_len : max;
    memcpy(ids, p->order, n * sizeof *ids);
    pthread_mutex_unlock(&p->lock);
    return n;
}

void pool_print_status(thread_pool_t *p, FILE *out)
{
    pthread_mutex_lock(&p->lock);
    fprintf(out, "Pool: %d threads (%d busy) | policy %s | %s | queued %zu | submitted %ld | done %ld\n",
            p->nworkers, p->active, p->policy == POLICY_FIFO ? "FIFO" : "PRIORITY",
            p->paused ? "PAUSED" : "running", p->size, p->submitted, p->completed);
    fprintf(out, "Threads: ");
    for (int i = 0; i < p->nworkers; i++) {
        const pool_worker_t *w = p->workers[i];
        if (w->busy)
            fprintf(out, "%s[T%d:job%d]%s ", clr(ANSI_YELLOW), i, w->current_job, clr(ANSI_RESET));
        else
            fprintf(out, "%s[T%d:idle]%s ", clr(ANSI_GREEN), i, clr(ANSI_RESET));
    }
    fprintf(out, "\nQueue (next first): ");
    size_t shown = p->size < 10 ? p->size : 10;
    job_t *copy = malloc(p->size * sizeof *copy + 1);
    if (copy) {
        memcpy(copy, p->heap, p->size * sizeof *copy);
        job_t *saved = p->heap;
        size_t saved_size = p->size;
        p->heap = copy;
        for (size_t i = 0; i < shown; i++) {
            job_t j = heap_pop(p);
            fprintf(out, "#%d(p%d) ", j.id, j.priority);
        }
        p->heap = saved;
        p->size = saved_size;
        free(copy);
    }
    if (p->size > shown)
        fprintf(out, "... +%zu more", p->size - shown);
    if (p->size == 0)
        fprintf(out, "(empty)");
    fprintf(out, "\n");
    if (p->completed > 0)
        fprintf(out, "Avg wait %.2f ms, avg run %.2f ms\n", p->total_wait_ms / (double)p->completed,
                p->total_run_ms / (double)p->completed);
    for (int i = 0; i < p->nworkers; i++)
        fprintf(out, "  T%-2d done %-5ld busy %.1f ms\n", i, p->workers[i]->done, p->workers[i]->busy_ms);
    pthread_mutex_unlock(&p->lock);
}

void pool_destroy(thread_pool_t *p)
{
    if (!p)
        return;
    pthread_mutex_lock(&p->resize_lock);
    pthread_mutex_lock(&p->lock);
    p->stopping = true;
    p->paused = false;
    pthread_cond_broadcast(&p->work_cv);
    int n = p->nworkers;
    pthread_mutex_unlock(&p->lock);
    for (int i = 0; i < n; i++) {
        pthread_join(p->workers[i]->tid, NULL);
        free(p->workers[i]);
    }
    pthread_mutex_unlock(&p->resize_lock);
    pthread_mutex_destroy(&p->lock);
    pthread_mutex_destroy(&p->resize_lock);
    pthread_cond_destroy(&p->work_cv);
    pthread_cond_destroy(&p->idle_cv);
    free(p->heap);
    free(p);
}

static thread_pool_t *g_pool = NULL;

void threads_cleanup(void)
{
    if (g_pool) {
        pool_destroy(g_pool);
        g_pool = NULL;
    }
}

static bool parse_policy(const char *s, sched_policy_t *out)
{
    if (strcmp(s, "fifo") == 0) { *out = POLICY_FIFO; return true; }
    if (strcmp(s, "prio") == 0 || strcmp(s, "priority") == 0) { *out = POLICY_PRIORITY; return true; }
    return false;
}

static void demo_policy(sched_policy_t policy)
{
    static const int prios[] = { 1, 5, 3, 9, 2, 9, 7, 0 };
    thread_pool_t *p = pool_create(1, policy);
    if (!p)
        return;
    pool_pause(p, true);
    for (int i = 0; i < 8; i++) {
        job_t j = { .id = i + 1, .type = JOB_SUM, .arg = 1000, .priority = prios[i] };
        snprintf(j.name, sizeof j.name, "demo%d", i + 1);
        pool_submit(p, j);
    }
    pool_pause(p, false);
    pool_wait_idle(p);
    int order[16];
    size_t n = pool_completed_order(p, order, 16);
    printf("  %-9s ran:", policy == POLICY_FIFO ? "FIFO" : "PRIORITY");
    for (size_t i = 0; i < n; i++)
        printf(" #%d(p%d)", order[i], prios[order[i] - 1]);
    printf("\n");
    pool_destroy(p);
}

cmd_status_t cmd_pool(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    long n;

    if (strcmp(sub, "demo") == 0) {
        printf("8 jobs submitted in order #1..#8 with priorities 1 5 3 9 2 9 7 0, one worker:\n");
        demo_policy(POLICY_FIFO);
        demo_policy(POLICY_PRIORITY);
        return CMD_OK;
    }
    if (strcmp(sub, "start") == 0) {
        sched_policy_t pol = POLICY_FIFO;
        n = 4;
        if (g_pool) {
            fprintf(stderr, "pool: already running (pool stop first)\n");
            return CMD_ERROR;
        }
        if ((argc > 2 && (!parse_long(argv[2], &n) || n < 1 || n > POOL_MAX_THREADS)) ||
            (argc > 3 && !parse_policy(argv[3], &pol))) {
            fprintf(stderr, "usage: pool start [threads 1..%d] [fifo|prio]\n", POOL_MAX_THREADS);
            return CMD_ERROR;
        }
        g_pool = pool_create((int)n, pol);
        if (!g_pool)
            return CMD_ERROR;
        printf("pool started: %ld threads, %s\n", n, pol == POLICY_FIFO ? "FIFO" : "PRIORITY");
        return CMD_OK;
    }
    if (!g_pool) {
        fprintf(stderr, "pool: not running (pool start [n] [fifo|prio], or pool demo)\n");
        return CMD_ERROR;
    }
    if (strcmp(sub, "submit") == 0) {
        job_t j;
        if (argc < 3 || job_parse(argc - 2, argv + 2, &j) != 0) {
            fprintf(stderr, "usage: pool submit <job> [arg] [priority]\n");
            return CMD_ERROR;
        }
        j.id = job_next_id();
        if (pool_submit(g_pool, j) != 0)
            return CMD_ERROR;
        printf("queued job %d (%s %ld, priority %d)\n", j.id, job_type_name(j.type), j.arg, j.priority);
    } else if (strcmp(sub, "batch") == 0) {
        job_t j;
        if (argc < 4 || !parse_long(argv[2], &n) || n < 1 || job_parse(argc - 3, argv + 3, &j) != 0) {
            fprintf(stderr, "usage: pool batch <count> <job> [arg]   (random priorities 0-9)\n");
            return CMD_ERROR;
        }
        unsigned seed = (unsigned)now_ms();
        for (long i = 0; i < n; i++) {
            job_t k = j;
            k.id = job_next_id();
            k.priority = (int)(rand_r(&seed) % 10);
            pool_submit(g_pool, k);
        }
        printf("queued %ld jobs\n", n);
    } else if (strcmp(sub, "status") == 0) {
        pool_print_status(g_pool, stdout);
    } else if (strcmp(sub, "resize") == 0) {
        if (argc < 3 || !parse_long(argv[2], &n) || pool_resize(g_pool, (int)n) != 0) {
            fprintf(stderr, "usage: pool resize <1..%d>\n", POOL_MAX_THREADS);
            return CMD_ERROR;
        }
        printf("pool resized to %ld threads\n", n);
    } else if (strcmp(sub, "policy") == 0) {
        sched_policy_t pol;
        if (argc < 3 || !parse_policy(argv[2], &pol)) {
            fprintf(stderr, "usage: pool policy fifo|prio\n");
            return CMD_ERROR;
        }
        pool_set_policy(g_pool, pol);
        printf("policy now %s (queued jobs re-ordered)\n", pol == POLICY_FIFO ? "FIFO" : "PRIORITY");
    } else if (strcmp(sub, "pause") == 0 || strcmp(sub, "resume") == 0) {
        pool_pause(g_pool, sub[0] == 'p');
        printf("pool %s\n", sub[0] == 'p' ? "paused (running jobs finish)" : "resumed");
    } else if (strcmp(sub, "wait") == 0) {
        if (pool_wait_idle(g_pool) != 0)
            fprintf(stderr, "pool: paused with queued jobs — resume first\n");
        pool_print_status(g_pool, stdout);
    } else if (strcmp(sub, "stop") == 0) {
        threads_cleanup();
        printf("pool stopped (queue drained)\n");
    } else {
        fprintf(stderr, "pool: unknown subcommand '%s'\n", sub);
        return CMD_ERROR;
    }
    return CMD_OK;
}
