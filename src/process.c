#include "minios.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const command_t cmds_process[] = {
    { "run",       cmd_run,       "run <program> [args...]",       "fork -> exec -> waitpid (Ch 4)" },
    { "processes", cmd_processes, "processes [-m|-c] [-n N] [-t]", "List processes from /proc (-m mem, -c cpu, -t my children)" },
    { NULL, NULL, NULL, NULL }
};

int run_program(char **args)
{
    pid_t pid;

    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        execvp(args[0], args);
        fprintf(stderr, "run: %s: %s\n", args[0], strerror(errno));
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return -1;
        }
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

cmd_status_t cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: run <program> [args...]\n");
        return CMD_ERROR;
    }
    double t0 = now_ms();
    int rc = run_program(argv + 1);
    double dt = now_ms() - t0;
    if (rc < 0)
        return CMD_ERROR;
    if (rc > 128)
        printf("[run] %s killed by signal %d (%s) after %.1f ms\n", argv[1], rc - 128, strsignal(rc - 128), dt);
    else
        printf("[run] %s exited with status %d after %.1f ms\n", argv[1], rc, dt);
    return rc == 0 ? CMD_OK : CMD_ERROR;
}

int proc_read(pid_t pid, process_info_t *out)
{
    char path[64], buf[1024];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');
    if (!lp || !rp || rp < lp)
        return -1;

    memset(out, 0, sizeof *out);
    out->pid = pid;
    size_t len = (size_t)(rp - lp - 1);
    if (len >= sizeof out->name)
        len = sizeof out->name - 1;
    memcpy(out->name, lp + 1, len);
    out->name[len] = '\0';

    int ppid;
    unsigned long long ut, st;
    long threads, rss_pages;
    /* fields 3..24 */
    int got = sscanf(rp + 2,
                     "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %ld %*d %*u %*u %ld",
                     &out->state, &ppid, &ut, &st, &threads, &rss_pages);
    if (got != 6)
        return -1;
    out->ppid = ppid;
    out->cpu_ticks = ut + st;
    out->threads = threads;
    out->rss_kb = rss_pages * (sysconf(_SC_PAGESIZE) / 1024);
    return 0;
}

int proc_list(process_info_t *out, int max)
{
    DIR *d = opendir("/proc");
    if (!d)
        return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (!isdigit((unsigned char)e->d_name[0]))
            continue;
        if (proc_read((pid_t)atoi(e->d_name), &out[n]) == 0)
            n++;
    }
    closedir(d);
    return n;
}

const char *proc_state_name(char s)
{
    switch (s) {
    case 'R': return "running";
    case 'S': return "sleeping";
    case 'D': return "disk-wait";
    case 'Z': return "zombie";
    case 'T': return "stopped";
    case 't': return "traced";
    case 'I': return "idle";
    case 'X': return "dead";
    default:  return "?";
    }
}

static int by_pid(const void *a, const void *b)
{
    return ((const process_info_t *)a)->pid - ((const process_info_t *)b)->pid;
}
static int by_mem(const void *a, const void *b)
{
    long x = ((const process_info_t *)a)->rss_kb, y = ((const process_info_t *)b)->rss_kb;
    return (x < y) - (x > y);
}
static int by_cpu(const void *a, const void *b)
{
    unsigned long long x = ((const process_info_t *)a)->cpu_ticks, y = ((const process_info_t *)b)->cpu_ticks;
    return (x < y) - (x > y);
}

cmd_status_t cmd_processes(int argc, char **argv)
{
    int (*cmp)(const void *, const void *) = by_pid;
    long limit = 0;
    bool children_only = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) cmp = by_mem;
        else if (strcmp(argv[i], "-c") == 0) cmp = by_cpu;
        else if (strcmp(argv[i], "-t") == 0) children_only = true;
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc && parse_long(argv[i + 1], &limit)) i++;
        else {
            fprintf(stderr, "usage: processes [-m|-c] [-n N] [-t]\n");
            return CMD_ERROR;
        }
    }

    enum { CAP = 8192 };
    process_info_t *list = malloc(sizeof *list * CAP);
    if (!list)
        return CMD_ERROR;
    int n = proc_list(list, CAP);
    if (n < 0) {
        perror("/proc");
        free(list);
        return CMD_ERROR;
    }
    qsort(list, (size_t)n, sizeof *list, cmp);

    long tick = sysconf(_SC_CLK_TCK);
    pid_t me = getpid();
    int counts[128] = { 0 }, shown = 0;
    printf("%s%7s %7s %-10s %4s %9s %9s  %s%s\n", clr(ANSI_CYAN), "PID", "PPID", "STATE", "THR", "RSS(KB)",
           "CPU(s)", "NAME", clr(ANSI_RESET));
    for (int i = 0; i < n; i++) {
        counts[(unsigned char)list[i].state & 127]++;
        if (children_only && list[i].ppid != me && list[i].pid != me)
            continue;
        if (limit > 0 && shown >= limit)
            continue;
        printf("%7d %7d %-10s %4ld %9ld %9.2f  %s%s\n", (int)list[i].pid, (int)list[i].ppid,
               proc_state_name(list[i].state), list[i].threads, list[i].rss_kb,
               (double)list[i].cpu_ticks / (double)tick, list[i].name,
               list[i].pid == me ? "  <- MiniOS" : "");
        shown++;
    }
    printf("%d processes: %d running, %d sleeping, %d disk-wait, %d zombie, %d stopped\n", n, counts['R'],
           counts['S'] + counts['I'], counts['D'], counts['Z'], counts['T']);
    free(list);
    return CMD_OK;
}
