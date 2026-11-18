#include "minios.h"

#include <string.h>

static double by_processes(int n, job_type_t type, long arg)
{
    double t0 = now_ms();
    for (int i = 1; i <= n; i++) {
        job_t j = { .id = i, .type = type, .arg = arg };
        snprintf(j.name, sizeof j.name, "%s-%d", job_type_name(type), i);
        job_result_t r;
        if (submit_job(&j, &r) != 0)
            return -1;
    }
    return now_ms() - t0;
}

static void row(const char *name, double ms, int n)
{
    if (ms < 0)
        printf("  %-24s failed\n", name);
    else
        printf("  %-24s %9.1f %12.3f\n", name, ms, ms / n);
}

cmd_status_t cmd_compare(int argc, char **argv)
{
    long n = 50, arg = 1000;
    job_type_t type = JOB_SUM;

    if (argc > 1 && (!parse_long(argv[1], &n) || n < 1 || n > 1000))
        goto usage;
    if (argc > 2) {
        int t = 0;
        while (t < JOB_STOP && strcmp(argv[2], job_type_name((job_type_t)t)) != 0)
            t++;
        if (t == JOB_STOP)
            goto usage;
        type = (job_type_t)t;
        arg = type == JOB_SLEEP ? 5 : 20000;
    }
    if (argc > 3 && (!parse_long(argv[3], &arg) || arg < 0))
        goto usage;

    printf("%ld %s jobs, arg %ld\n", n, job_type_name(type), arg);
    printf("  %-24s %9s %12s\n", "method", "total ms", "ms per job");
    row("processes, one by one", by_processes((int)n, type, arg), (int)n);
    row("1 thread", workers_time(1, (int)n, type, arg), (int)n);
    row("4 threads", workers_time(4, (int)n, type, arg), (int)n);
    return CMD_OK;

usage:
    fprintf(stderr, "usage: compare [jobs 1..1000] [type] [arg]\n");
    return CMD_ERROR;
}
