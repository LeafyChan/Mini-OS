#include "minios.h"

#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int set_one(pid_t pid, int resource, rlim_t soft, rlim_t hard)
{
    struct rlimit rl = { .rlim_cur = soft, .rlim_max = hard };
    if (pid == 0)
        return setrlimit(resource, &rl);
    return prlimit(pid, resource, &rl, NULL);
}

static int apply(pid_t pid, const exec_limits_t *lim)
{
    if (lim->cpu_seconds > 0 &&
        set_one(pid, RLIMIT_CPU, (rlim_t)lim->cpu_seconds, (rlim_t)lim->cpu_seconds + 1) != 0)
        return -1;
    if (lim->mem_mb > 0) {
        rlim_t bytes = (rlim_t)lim->mem_mb * 1024 * 1024;
        if (set_one(pid, RLIMIT_AS, bytes, bytes) != 0)
            return -1;
    }
    if (lim->max_files > 0 &&
        set_one(pid, RLIMIT_NOFILE, (rlim_t)lim->max_files, (rlim_t)lim->max_files) != 0)
        return -1;
    return 0;
}

int apply_limits_to_pid(pid_t pid, const exec_limits_t *lim)
{
    return apply(pid, lim);
}

int run_limited(char **args, const exec_limits_t *lim)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        if (apply(0, lim) != 0) {
            perror("setrlimit");
            _exit(126);
        }
        execvp(args[0], args);
        fprintf(stderr, "runlimit: %s: %s\n", args[0], strerror(errno));
        _exit(127);
    }

    int status;
    struct rusage ru;
    double t0 = now_ms();
    while (wait4(pid, &status, 0, &ru) < 0) {
        if (errno != EINTR) {
            perror("wait4");
            return -1;
        }
    }
    double wall = now_ms() - t0;
    double cpu = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1e6 +
                 (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec / 1e6;

    printf("[runlimit] %s: wall %.2f s, CPU %.2f s, peak RSS %ld KB\n", args[0], wall / 1000, cpu,
           ru.ru_maxrss);
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        const char *why = sig == SIGXCPU ? "CPU soft limit reached"
                        : sig == SIGKILL ? "killed (CPU hard limit or OOM)"
                        : sig == SIGSEGV ? "crashed (often: allocation failed and was not checked)"
                        : "terminated";
        printf("[runlimit] -> killed by SIG%s: %s\n", sigabbrev_np(sig), why);
        return 128 + sig;
    }
    printf("[runlimit] -> exited with status %d\n", WEXITSTATUS(status));
    return WEXITSTATUS(status);
}

cmd_status_t cmd_runlimit(int argc, char **argv)
{
    exec_limits_t lim = { 0 };
    int opt;
    optind = 1;
    opterr = 0;
    while ((opt = getopt(argc, argv, "+c:m:f:")) != -1) {
        long v;
        if (opt == '?' || !parse_long(optarg, &v) || v <= 0)
            goto usage;
        if (opt == 'c') lim.cpu_seconds = v;
        if (opt == 'm') lim.mem_mb = v;
        if (opt == 'f') lim.max_files = v;
    }
    if (optind >= argc)
        goto usage;
    printf("[runlimit] limits: cpu=%lds mem=%ldMB files=%ld (0 = unlimited)\n", lim.cpu_seconds, lim.mem_mb,
           lim.max_files);
    return run_limited(argv + optind, &lim) == 0 ? CMD_OK : CMD_ERROR;
usage:
    fprintf(stderr, "usage: runlimit [-c cpu_sec] [-m mem_MB] [-f max_files] <program> [args...]\n");
    return CMD_ERROR;
}

static void fmt_lim(rlim_t v, rlim_t div, const char *unit, char *buf, size_t cap)
{
    if (v == RLIM_INFINITY)
        snprintf(buf, cap, "unlimited");
    else
        snprintf(buf, cap, "%llu%s", (unsigned long long)(v / div), unit);
}

static void print_limit(pid_t pid, int res, const char *name, const char *unit, rlim_t div)
{
    struct rlimit rl;
    if (prlimit(pid, res, NULL, &rl) != 0) {
        printf("  %-6s %s\n", name, strerror(errno));
        return;
    }
    char soft[32], hard[32];
    fmt_lim(rl.rlim_cur, div, unit, soft, sizeof soft);
    fmt_lim(rl.rlim_max, div, unit, hard, sizeof hard);
    printf("  %-6s soft %-12s hard %s\n", name, soft, hard);
}

cmd_status_t cmd_limit(int argc, char **argv)
{
    long id;
    if (argc < 2 || !parse_long(argv[1][0] == '%' ? argv[1] + 1 : argv[1], &id)) {
        fprintf(stderr, "usage: limit <job_id|pid> [cpu <sec>] [mem <MB>] [files <n>]\n");
        return CMD_ERROR;
    }
    bg_job_t job;
    if (!bg_find((int)id, &job) || (job.state != BG_RUNNING && job.state != BG_STOPPED)) {
        fprintf(stderr, "limit: %ld is not a running MiniOS background job\n", id);
        return CMD_ERROR;
    }

    exec_limits_t lim = { 0 };
    for (int i = 2; i + 1 < argc; i += 2) {
        long v;
        if (!parse_long(argv[i + 1], &v) || v <= 0) {
            fprintf(stderr, "limit: bad value '%s'\n", argv[i + 1]);
            return CMD_ERROR;
        }
        if (strcmp(argv[i], "cpu") == 0) lim.cpu_seconds = v;
        else if (strcmp(argv[i], "mem") == 0) lim.mem_mb = v;
        else if (strcmp(argv[i], "files") == 0) lim.max_files = v;
        else {
            fprintf(stderr, "limit: unknown resource '%s'\n", argv[i]);
            return CMD_ERROR;
        }
    }
    if (argc % 2 != 0) {
        fprintf(stderr, "limit: each resource needs a value\n");
        return CMD_ERROR;
    }

    if (argc > 2) {
        if (apply_limits_to_pid(job.pid, &lim) != 0) {
            perror("prlimit");
            return CMD_ERROR;
        }
        printf("job %d (pid %d) limits updated\n", job.id, (int)job.pid);
        if (lim.cpu_seconds > 0)
            printf("  note: RLIMIT_CPU counts CPU already used; a job past the limit gets SIGXCPU at once\n");
    }
    printf("limits of job %d (pid %d, %s):\n", job.id, (int)job.pid, job.cmdline);
    print_limit(job.pid, RLIMIT_CPU, "cpu", "s", 1);
    print_limit(job.pid, RLIMIT_AS, "mem", "MB", 1024 * 1024);
    print_limit(job.pid, RLIMIT_NOFILE, "files", "", 1);
    return CMD_OK;
}
