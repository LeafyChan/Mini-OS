#include "minios.h"

#include <stdlib.h>
#include <string.h>

void *worker_main(void *arg)
{
    worker_context_t *ctx = arg;
    block_signals_in_thread();

    while (!atomic_load(&ctx->shutdown)) {
        job_t job;
        if (queue_pop(ctx->queue, &job) != 0)           /* TODO 1: obtain a job */
            break;
        if (job.type == JOB_STOP)                       /* poison pill: wakes a thread blocked on full */
            break;

        char text[RESULT_MAX];
        double t0 = now_ms();
        long value = job_execute(&job, text, sizeof text);      /* TODO 2: process it */
        double dt = now_ms() - t0;

        ctx->jobs_done++;                               /* TODO 3: record (ctx is thread-private) */
        ctx->result_sum += value;
        ctx->busy_ms += dt;

        if (ctx->verbose)
            printf("  [T%d] job %-3d %-34s %7.2f ms\n", ctx->worker_id, job.id, text, dt);
    }
    return NULL;
}

static int run_threads(int nthreads, int njobs, job_type_t type, long arg,
                       bool verbose, bool quiet, double *elapsed_ms)
{
    if (nthreads < 1 || nthreads > 64 || njobs < 0)
        return -1;

    job_queue_t q;
    if (queue_init(&q) != 0)
        return -1;

    worker_context_t *w = calloc((size_t)nthreads, sizeof *w);
    if (w == NULL) {
        queue_destroy(&q);
        return -1;
    }

    double t0 = now_ms();
    int started = 0;
    for (; started < nthreads; started++) {
        w[started].worker_id = started + 1;
        w[started].queue = &q;
        w[started].verbose = verbose;
        atomic_init(&w[started].shutdown, false);
        if (pthread_create(&w[started].thread, NULL, worker_main, &w[started]) != 0)
            break;
    }
    if (started == 0) {
        free(w);
        queue_destroy(&q);
        return -1;
    }

    for (int i = 1; i <= njobs; i++) {
        job_t j = { .id = i, .type = type, .arg = arg, .seq = (uint64_t)i, .submit_ms = now_ms() };
        snprintf(j.name, sizeof j.name, "%s-%d", job_type_name(type), i);
        queue_push(&q, j);
    }
    for (int i = 0; i < started; i++) {
        job_t stop = { .type = JOB_STOP };
        queue_push(&q, stop);
    }
    for (int i = 0; i < started; i++)
        pthread_join(w[i].thread, NULL);

    double dt = now_ms() - t0;
    long done = 0;
    for (int i = 0; i < started; i++)
        done += w[i].jobs_done;

    if (!quiet) {
        for (int i = 0; i < started; i++)
            printf("  T%-2d  %3ld jobs  busy %8.1f ms\n", w[i].worker_id, w[i].jobs_done, w[i].busy_ms);
        printf("  %ld/%d jobs finished by %d threads in %.1f ms\n", done, njobs, started, dt);
    }

    if (elapsed_ms)
        *elapsed_ms = dt;
    free(w);
    queue_destroy(&q);
    return done == njobs ? 0 : -1;
}

int workers_run(int nthreads, int njobs, job_type_t type, long arg,
                bool verbose, double *elapsed_ms)
{
    return run_threads(nthreads, njobs, type, arg, verbose, false, elapsed_ms);
}

/* silent version, returns the time in ms or -1 */
double workers_time(int nthreads, int njobs, job_type_t type, long arg)
{
    double ms = 0;
    return run_threads(nthreads, njobs, type, arg, false, true, &ms) == 0 ? ms : -1;
}

static bool parse_type(const char *s, job_type_t *out)
{
    for (job_type_t t = JOB_SUM; t < JOB_STOP; t++)
        if (strcmp(s, job_type_name(t)) == 0) {
            *out = t;
            return true;
        }
    return false;
}

static cmd_status_t workers_bench(long njobs, job_type_t type, long arg)
{
    static const int counts[] = { 1, 2, 4, 8 };
    double base = 0;

    printf("%ld %s jobs (arg %ld):\n", njobs, job_type_name(type), arg);
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        double ms;
        if (workers_run(counts[i], (int)njobs, type, arg, false, &ms) != 0)
            return CMD_ERROR;
        if (i == 0)
            base = ms;
        printf("  => %d thread(s): %.1f ms, speed-up %.2fx\n\n", counts[i], ms, base / ms);
    }
    return CMD_OK;
}

cmd_status_t cmd_workers(int argc, char **argv)
{
    long nthreads = 3, njobs = 12, arg = 100000;
    job_type_t type = JOB_SUM;
    bool verbose = true, bench = false;
    int pos = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "bench") == 0) { bench = true; continue; }
        if (strcmp(argv[i], "-q") == 0)    { verbose = false; continue; }
        bool ok = true;
        switch (pos++) {
        case 0:  ok = bench ? parse_long(argv[i], &njobs) : parse_long(argv[i], &nthreads); break;
        case 1:  ok = bench ? parse_type(argv[i], &type)  : parse_long(argv[i], &njobs);    break;
        case 2:  ok = bench ? parse_long(argv[i], &arg)   : parse_type(argv[i], &type);     break;
        case 3:  ok = !bench && parse_long(argv[i], &arg); break;
        default: ok = false;
        }
        if (!ok) {
            fprintf(stderr, "usage: workers [threads] [jobs] [type] [arg] [-q] | workers bench [jobs] [type] [arg]\n");
            return CMD_ERROR;
        }
    }

    if (nthreads < 1 || nthreads > 64 || njobs < 0 || njobs > 100000 || arg < 0) {
        fprintf(stderr, "workers: threads must be 1..64, jobs 0..100000, arg >= 0\n");
        return CMD_ERROR;
    }

    if (bench) {
        if (pos < 2) type = JOB_PRIMES;
        if (pos < 3) arg = 60000;
        if (pos < 1) njobs = 16;
        return workers_bench(njobs, type, arg);
    }
    return workers_run((int)nthreads, (int)njobs, type, arg, verbose, NULL) == 0 ? CMD_OK : CMD_ERROR;
}

const command_t cmds_threads[] = {
    { "workers", cmd_workers, "workers [threads] [jobs] [type] [arg] [-q]",
      "threads drain the 5 slot queue, workers bench times 1, 2, 4 and 8 threads" },
    { "race",    cmd_race,         "race [iters] [threads] [trials]",
      "unsafe counter against semaphore, mutex and atomic" },
    { NULL, NULL, NULL, NULL }
};
