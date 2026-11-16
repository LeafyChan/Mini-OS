/* jobs.c: job definitions, submit and jobs (handbook 5.3). owner m3
 *
 *   MiniOS (manager) --fork--> worker process --does work-->
 *        ^                                         |
 *        +------------- pipe <---------------------+
 */
#include "minios.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const command_t cmds_ipc[] = {
    { "submit",   cmd_submit,   "submit <job> [arg] [priority]", "run a job in a worker process, result comes back over a pipe" },
    { "jobs",     cmd_jobs,     "jobs",                          "table of submitted jobs and their results" },
    { "ipcbench", cmd_ipcbench, "ipcbench [msgs] [bytes] | sweep", "pipe against shared memory" },
    { "compare",  cmd_compare,  "compare [jobs] [type] [arg]",   "same jobs run by processes and by threads" },
    { NULL, NULL, NULL, NULL }
};

static const char *type_names[] = { "sum", "primes", "fib", "hash", "sleep", "stop" };
static const long default_args[] = { 1000000, 100000, 50, 200000, 200, 0 };

typedef struct {
    job_t        job;
    job_result_t result;
    int          status;          /* 0 ok, else error */
} job_record_t;

static job_record_t history[MAX_SUBMITTED];
static int history_len = 0;
static atomic_int id_counter = 0;

int job_next_id(void)
{
    return atomic_fetch_add(&id_counter, 1) + 1;
}

const char *job_type_name(job_type_t t)
{
    return (unsigned)t < sizeof type_names / sizeof type_names[0] ? type_names[t] : "?";
}

/* "sum 100 5"  -> type sum, arg 100, priority 5
 * "job1"       -> type hash over the text "job1" (handbook: submit job1) */
int job_parse(int argc, char **argv, job_t *out)
{
    if (argc < 1)
        return -1;
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", argv[0]);
    out->type = JOB_HASH;
    out->arg = default_args[JOB_HASH];
    for (int t = 0; t < JOB_STOP; t++) {
        if (strcmp(argv[0], type_names[t]) == 0) {
            out->type = (job_type_t)t;
            out->arg = default_args[t];
        }
    }
    long v;
    if (argc > 1) {
        if (!parse_long(argv[1], &v) || v < 0)
            return -1;
        out->arg = v;
    }
    if (argc > 2) {
        if (!parse_long(argv[2], &v))
            return -1;
        out->priority = (int)v;
    }
    if (out->type == JOB_FIB && out->arg > 100000000)
        return -1;
    if (out->type == JOB_SUM && out->arg > 4000000000L)   /* n(n+1)/2 must fit in a long */
        return -1;
    return 0;
}

static bool is_prime(long n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (long d = 3; d * d <= n; d += 2)
        if (n % d == 0)
            return false;
    return true;
}

/* Pure function of its input: safe to call from many threads at once. */
long job_execute(const job_t *job, char *text, size_t cap)
{
    long v = 0;
    switch (job->type) {
    case JOB_SUM: {
        long s = 0;
        for (long i = 1; i <= job->arg; i++)
            s += i;
        v = s;
        snprintf(text, cap, "sum(1..%ld) = %ld", job->arg, v);
        break;
    }
    case JOB_PRIMES:
        for (long i = 2; i <= job->arg; i++)
            v += is_prime(i);
        snprintf(text, cap, "primes <= %ld: %ld", job->arg, v);
        break;
    case JOB_FIB: {
        const long mod = 1000000007L;
        long a = 0, b = 1;
        for (long i = 0; i < job->arg; i++) {
            long c = (a + b) % mod;
            a = b;
            b = c;
        }
        v = a;
        snprintf(text, cap, "fib(%ld) mod 1e9+7 = %ld", job->arg, v);
        break;
    }
    case JOB_HASH: {
        uint64_t h = 1469598103934665603ULL;
        size_t n = strlen(job->name);
        for (long r = 0; r < (job->arg > 0 ? job->arg : 1); r++)
            for (size_t i = 0; i < n; i++)
                h = (h ^ (unsigned char)job->name[i]) * 1099511628211ULL;
        v = (long)(h & 0x7fffffffffffffffULL);
        snprintf(text, cap, "hash('%s', %ld rounds) = %016llx", job->name, job->arg, (unsigned long long)h);
        break;
    }
    case JOB_SLEEP:
        sleep_ms(job->arg);
        v = job->arg;
        snprintf(text, cap, "slept %ld ms", job->arg);
        break;
    case JOB_STOP:
        snprintf(text, cap, "stop");
        break;
    }
    return v;
}

int submit_job(const job_t *job, job_result_t *out)
{
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return -1;
    }

    if (pid == 0) {                           /* worker process */
        close(fd[0]);                         /* worker only writes */
        job_result_t r = { .job_id = job->id, .worker_pid = getpid(), .worker_id = -1 };
        double t0 = now_ms();
        r.value = job_execute(job, r.text, sizeof r.text);
        r.elapsed_ms = now_ms() - t0;
        int rc = send_job_result(fd[1], &r);
        close(fd[1]);
        _exit(rc == 0 ? 0 : 1);
    }

    close(fd[1]);                             /* manager only reads: now EOF can happen */
    int rc = receive_job_result(fd[0], out);
    close(fd[0]);

    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (rc != 0) {
        fprintf(stderr, "submit: worker %d sent no result (%s)\n", (int)pid,
                WIFSIGNALED(status) ? strsignal(WTERMSIG(status)) : "exited early");
        return -1;
    }
    return 0;
}

cmd_status_t cmd_submit(int argc, char **argv)
{
    job_t job;
    if (argc < 2 || job_parse(argc - 1, argv + 1, &job) != 0) {
        fprintf(stderr, "usage: submit <sum|primes|fib|hash|sleep|any-name> [arg] [priority]\n");
        return CMD_ERROR;
    }
    job.id = job_next_id();
    job.submit_ms = now_ms();

    job_result_t res;
    int rc = submit_job(&job, &res);

    job_record_t *rec = &history[history_len % MAX_SUBMITTED];
    history_len++;
    rec->job = job;
    rec->status = rc;
    if (rc == 0)
        rec->result = res;
    else
        memset(&rec->result, 0, sizeof rec->result);

    if (rc != 0)
        return CMD_ERROR;
    printf("[job %d] %s  (worker pid %d, %.2f ms, via pipe)\n", job.id, res.text, (int)res.worker_pid,
           res.elapsed_ms);
    return CMD_OK;
}

cmd_status_t cmd_jobs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (history_len == 0) {
        printf("(no jobs yet, try: submit primes 50000)\n");
        return CMD_OK;
    }
    printf("%s%4s %-10s %-7s %9s %4s %8s %9s  %s%s\n", clr(ANSI_CYAN), "ID", "NAME", "TYPE", "ARG", "PRIO",
           "WORKER", "TIME(ms)", "RESULT", clr(ANSI_RESET));
    int start = history_len > MAX_SUBMITTED ? history_len - MAX_SUBMITTED : 0;
    for (int k = start; k < history_len; k++) {
        const job_record_t *r = &history[k % MAX_SUBMITTED];
        printf("%4d %-10.10s %-7s %9ld %4d %8d %9.2f  %s\n", r->job.id, r->job.name, job_type_name(r->job.type),
               r->job.arg, r->job.priority, (int)r->result.worker_pid, r->result.elapsed_ms,
               r->status == 0 ? r->result.text : "FAILED");
    }
    return CMD_OK;
}
