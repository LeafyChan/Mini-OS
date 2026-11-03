#include "minios.h"
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

int read_uptime(double *uptime_s, double *idle_s)
{
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp == NULL)
        return -1;

    int n = fscanf(fp, "%lf %lf", uptime_s, idle_s);
    fclose(fp);

    return n == 2 ? 0 : -1;
}

int read_meminfo(mem_info_t *m)
{
    memset(m, 0, sizeof *m);

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL)
        return -1;

    char line[256], key[64];
    unsigned long value;
    int found = 0;

    while (fgets(line, sizeof line, fp)) {
        if (sscanf(line, "%63[^:]: %lu", key, &value) != 2)
            continue;

        if      (strcmp(key, "MemTotal")     == 0) m->total_kb      = value;
        else if (strcmp(key, "MemFree")      == 0) m->free_kb       = value;
        else if (strcmp(key, "MemAvailable") == 0) m->available_kb  = value;
        else if (strcmp(key, "Buffers")      == 0) m->buffers_kb    = value;
        else if (strcmp(key, "Cached")       == 0) m->cached_kb     = value;
        else if (strcmp(key, "SwapTotal")    == 0) m->swap_total_kb = value;
        else if (strcmp(key, "SwapFree")     == 0) m->swap_free_kb  = value;
        else continue;

        found++;
    }
    fclose(fp);

    return found > 0 ? 0 : -1;
}

int read_loadavg(load_info_t *l)
{
    FILE *fp = fopen("/proc/loadavg", "r");
    if (fp == NULL)
        return -1;

    int n = fscanf(fp, "%lf %lf %lf %d/%d %d", &l->load1, &l->load5, &l->load15,
                   &l->runnable, &l->entities, &l->last_pid);
    fclose(fp);

    return n == 6 ? 0 : -1;
}

void show_system_info(void)
{
    double up = 0, idle = 0;
    mem_info_t mem;
    load_info_t load;
    struct utsname uts;
    char buf[64];
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

    printf("%s== System information (from /proc) ==%s\n", clr(ANSI_BOLD), clr(ANSI_RESET));

    if (uname(&uts) == 0)
        printf("  Host        : %s  (%s %s, %s)\n",
               uts.nodename, uts.sysname, uts.release, uts.machine);

    if (read_uptime(&up, &idle) == 0) {
        fmt_duration(up, buf, sizeof buf);
        printf("  Uptime      : %.2f s  (%s)\n", up, buf);
        /* idle is added up over every CPU, so it can exceed the uptime */
        printf("  Idle time   : %.2f s  (summed over %ld CPUs)\n", idle, ncpu);
    } else {
        printf("  Uptime      : unavailable\n");
    }

    if (read_meminfo(&mem) == 0 && mem.total_kb > 0) {
        unsigned long used = mem.total_kb - mem.available_kb;
        double frac = (double)used / (double)mem.total_kb;

        printf("  Memory      : %lu MB total, %lu MB available, %lu MB free\n",
               mem.total_kb / 1024, mem.available_kb / 1024, mem.free_kb / 1024);
        printf("  Memory used : ");
        print_bar(stdout, frac, 30);
        printf(" %.1f%%  (cache %lu MB)\n", 100 * frac, mem.cached_kb / 1024);

        if (mem.swap_total_kb > 0)
            printf("  Swap        : %lu / %lu MB used\n",
                   (mem.swap_total_kb - mem.swap_free_kb) / 1024, mem.swap_total_kb / 1024);
        else
            printf("  Swap        : none configured\n");
    } else {
        printf("  Memory      : unavailable\n");
    }

    if (read_loadavg(&load) == 0) {
        printf("  Load avg    : %.2f (1m)  %.2f (5m)  %.2f (15m)\n",
               load.load1, load.load5, load.load15);
        printf("  Tasks       : %d runnable of %d, last PID %d\n",
               load.runnable, load.entities, load.last_pid);
    }

    printf("  MiniOS PID  : %d  (look at /proc/%d/status)\n", (int)getpid(), (int)getpid());
}

cmd_status_t cmd_system(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    show_system_info();
    return CMD_OK;
}
