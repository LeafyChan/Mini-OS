#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define N 300

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double bench_fork_only(void)
{
    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        pid_t p = fork();
        if (p == 0)
            _exit(0);
        waitpid(p, NULL, 0);
    }
    return (now_ms() - t0) / N;
}

static double bench_fork_exec(void)
{
    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        pid_t p = fork();
        if (p == 0) {
            execl("/bin/true", "true", (char *)NULL);
            _exit(127);
        }
        waitpid(p, NULL, 0);
    }
    return (now_ms() - t0) / N;
}

#define REPS 7

static int cmp_double(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

static double median_of(double (*fn)(void), double *lo, double *hi)
{
    double v[REPS];
    for (int i = 0; i < REPS; i++)
        v[i] = fn();
    qsort(v, REPS, sizeof v[0], cmp_double);
    *lo = v[0];
    *hi = v[REPS - 1];
    return v[REPS / 2];
}

int main(void)
{
    bench_fork_only();
    bench_fork_exec();

    double alo, ahi, blo, bhi;
    double a = median_of(bench_fork_only, &alo, &ahi);
    double b = median_of(bench_fork_exec, &blo, &bhi);

    printf("e8 %-26s %8.3f ms   (range %.3f - %.3f over %d reps)\n",
           "fork + _exit + wait", a, alo, ahi, REPS);
    printf("e8 %-26s %8.3f ms   (range %.3f - %.3f over %d reps)\n",
           "fork + exec + wait", b, blo, bhi, REPS);
    printf("e8 %-26s %8.3f ms\n", "cost of exec alone", b - a);
    printf("e8 %-26s %8.2f x    (worst case %.2f, best case %.2f)\n",
           "exec makes it slower by", b / a, bhi / alo, blo / ahi);
    return 0;
}
