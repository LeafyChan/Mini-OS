#include "minios.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bg_job_t table[MAX_BG_JOBS];
static int next_id = 1;
static volatile sig_atomic_t finished_pending = 0;

static double signal_safe_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void on_sigchld(int sig)
{
    (void)sig;
    int saved_errno = errno;
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        bg_job_t *j = &table[i];
        if (j->state != BG_RUNNING && j->state != BG_STOPPED)
            continue;
        int status;
        pid_t r = waitpid(j->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (r != j->pid)
            continue;
        if (WIFEXITED(status)) {
            j->state = BG_DONE;
            j->exit_code = WEXITSTATUS(status);
            j->end_ms = signal_safe_now_ms();
            finished_pending = 1;
        } else if (WIFSIGNALED(status)) {
            j->state = BG_SIGNALED;
            j->term_signal = WTERMSIG(status);
            j->end_ms = signal_safe_now_ms();
            finished_pending = 1;
        } else if (WIFSTOPPED(status)) {
            j->state = BG_STOPPED;
        } else if (WIFCONTINUED(status)) {
            j->state = BG_RUNNING;
        }
    }
    errno = saved_errno;
}

static void block_chld(sigset_t *old)
{
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGCHLD);
    sigprocmask(SIG_BLOCK, &s, old);
}

static void restore_mask(const sigset_t *old)
{
    sigprocmask(SIG_SETMASK, old, NULL);
}

void bg_init(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
}

static const char *state_label(const bg_job_t *j, char *buf, size_t cap)
{
    switch (j->state) {
    case BG_RUNNING:  snprintf(buf, cap, "Running"); break;
    case BG_STOPPED:  snprintf(buf, cap, "Stopped"); break;
    case BG_DONE:     snprintf(buf, cap, "Done(%d)", j->exit_code); break;
    case BG_SIGNALED: snprintf(buf, cap, "Killed(%s)", sigabbrev_np(j->term_signal) ? sigabbrev_np(j->term_signal) : "?"); break;
    default:          snprintf(buf, cap, "-"); break;
    }
    return buf;
}

int run_background(char **args, const char *cmdline)
{
    sigset_t old;
    block_chld(&old);

    int slot = -1;
    for (int i = 0; i < MAX_BG_JOBS && slot < 0; i++)
        if (table[i].state == BG_FREE)
            slot = i;
    for (int i = 0; i < MAX_BG_JOBS && slot < 0; i++)
        if ((table[i].state == BG_DONE || table[i].state == BG_SIGNALED) && table[i].reported)
            slot = i;
    if (slot < 0) {
        restore_mask(&old);
        fprintf(stderr, "bg: job table full (%d)\n", MAX_BG_JOBS);
        return -1;
    }

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        restore_mask(&old);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        signal(SIGINT, SIG_DFL);
        restore_mask(&old);
        execvp(args[0], args);
        fprintf(stderr, "bg: %s: %s\n", args[0], strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);

    bg_job_t *j = &table[slot];
    memset(j, 0, sizeof *j);
    j->id = next_id++;
    j->pid = pid;
    j->start_ms = now_ms();
    snprintf(j->cmdline, sizeof j->cmdline, "%s", cmdline);
    j->state = BG_RUNNING;
    int id = j->id;
    restore_mask(&old);

    printf("[%d] %d\n", id, (int)pid);
    return id;
}

void bg_report_finished(void)
{
    if (!finished_pending)
        return;
    sigset_t old;
    block_chld(&old);
    finished_pending = 0;
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        bg_job_t *j = &table[i];
        if ((j->state == BG_DONE || j->state == BG_SIGNALED) && !j->reported) {
            char st[32];
            printf("[%d]+ %-12s %-30s (pid %d, %.2f s)\n", j->id, state_label(j, st, sizeof st), j->cmdline,
                   (int)j->pid, (j->end_ms - j->start_ms) / 1000.0);
            j->reported = true;
        }
    }
    restore_mask(&old);
}

bool bg_find(int id_or_pid, bg_job_t *copy)
{
    sigset_t old;
    block_chld(&old);
    bool found = false;
    for (int i = 0; i < MAX_BG_JOBS && !found; i++) {
        if (table[i].state != BG_FREE && (table[i].id == id_or_pid || table[i].pid == id_or_pid)) {
            *copy = table[i];
            found = true;
        }
    }
    restore_mask(&old);
    return found;
}

cmd_status_t cmd_ps(int argc, char **argv)
{
    bool clear = argc > 1 && strcmp(argv[1], "-c") == 0;
    sigset_t old;
    block_chld(&old);
    bg_job_t snap[MAX_BG_JOBS];
    memcpy(snap, table, sizeof snap);
    if (clear)
        for (int i = 0; i < MAX_BG_JOBS; i++)
            if (table[i].state == BG_DONE || table[i].state == BG_SIGNALED)
                table[i].state = BG_FREE;
    restore_mask(&old);

    long tick = sysconf(_SC_CLK_TCK);
    int shown = 0;
    printf("%s%4s %7s %-14s %9s %9s %9s  %s%s\n", clr(ANSI_CYAN), "JOB", "PID", "STATE", "ELAPSED", "CPU(s)",
           "RSS(KB)", "COMMAND", clr(ANSI_RESET));
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        bg_job_t *j = &snap[i];
        if (j->state == BG_FREE)
            continue;
        char st[32];
        bool live = j->state == BG_RUNNING || j->state == BG_STOPPED;
        double elapsed = ((live ? now_ms() : j->end_ms) - j->start_ms) / 1000.0;
        process_info_t pi;
        bool have = live && proc_read(j->pid, &pi) == 0;
        printf("%4d %7d %-14s %8.2fs ", j->id, (int)j->pid, state_label(j, st, sizeof st), elapsed);
        if (have)
            printf("%9.2f %9ld", (double)pi.cpu_ticks / (double)tick, pi.rss_kb);
        else
            printf("%9s %9s", "-", "-");
        printf("  %s\n", j->cmdline);
        shown++;
    }
    if (shown == 0)
        printf("(no background jobs — try: run sleep 5 &)\n");
    if (clear)
        printf("finished jobs cleared\n");
    return CMD_OK;
}

static bool any_running(int id)
{
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        bool live = table[i].state == BG_RUNNING || table[i].state == BG_STOPPED;
        if (live && (id == 0 || table[i].id == id))
            return true;
    }
    return false;
}

cmd_status_t cmd_wait(int argc, char **argv)
{
    long id = 0;
    if (argc > 1 && (!parse_long(argv[1][0] == '%' ? argv[1] + 1 : argv[1], &id) || id <= 0)) {
        fprintf(stderr, "usage: wait [job_id]\n");
        return CMD_ERROR;
    }
    bg_job_t tmp;
    if (id > 0 && !bg_find((int)id, &tmp)) {
        fprintf(stderr, "wait: no job %ld\n", id);
        return CMD_ERROR;
    }

    sigset_t old;
    block_chld(&old);
    sigset_t during = old;
    sigdelset(&during, SIGCHLD);
    sigdelset(&during, SIGINT);
    while (any_running((int)id) && !minios_interrupted)
        sigsuspend(&during);
    restore_mask(&old);
    if (minios_interrupted)
        printf("wait: interrupted\n");
    bg_report_finished();
    return CMD_OK;
}

static int parse_signal(const char *s)
{
    static const struct { const char *name; int sig; } sigs[] = {
        { "TERM", SIGTERM }, { "KILL", SIGKILL }, { "STOP", SIGSTOP },
        { "CONT", SIGCONT }, { "INT", SIGINT },   { "HUP", SIGHUP },
    };
    if (strncmp(s, "SIG", 3) == 0)
        s += 3;
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        if (strcmp(s, sigs[i].name) == 0)
            return sigs[i].sig;
    long n;
    return parse_long(s, &n) && n > 0 && n < NSIG ? (int)n : -1;
}

cmd_status_t cmd_kill(int argc, char **argv)
{
    long id;
    if (argc < 2 || !parse_long(argv[1][0] == '%' ? argv[1] + 1 : argv[1], &id)) {
        fprintf(stderr, "usage: kill <job_id> [TERM|KILL|STOP|CONT|INT]\n");
        return CMD_ERROR;
    }
    int sig = argc > 2 ? parse_signal(argv[2]) : SIGTERM;
    if (sig < 0) {
        fprintf(stderr, "kill: unknown signal '%s'\n", argv[2]);
        return CMD_ERROR;
    }
    bg_job_t j;
    if (!bg_find((int)id, &j) || (j.state != BG_RUNNING && j.state != BG_STOPPED)) {
        fprintf(stderr, "kill: no running job %ld (only MiniOS's own jobs can be signalled)\n", id);
        return CMD_ERROR;
    }
    if (kill(j.pid, sig) != 0) {
        perror("kill");
        return CMD_ERROR;
    }
    if (sig == SIGSTOP || sig == SIGCONT)
        sleep_ms(50);
    printf("sent SIG%s to job %d (pid %d)\n", sigabbrev_np(sig), j.id, (int)j.pid);
    return CMD_OK;
}

void bg_cleanup(void)
{
    sigset_t old;
    block_chld(&old);
    int n = 0;
    for (int i = 0; i < MAX_BG_JOBS; i++) {
        if (table[i].state == BG_RUNNING || table[i].state == BG_STOPPED) {
            kill(table[i].pid, SIGTERM);
            kill(table[i].pid, SIGCONT);
            waitpid(table[i].pid, NULL, 0);
            table[i].state = BG_FREE;
            n++;
        }
    }
    restore_mask(&old);
    if (n > 0)
        printf("terminated %d background job(s)\n", n);
}
