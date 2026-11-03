#include "minios.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long times_idle(const cpu_times_t *t)
{
    return t->idle + t->iowait;
}

static unsigned long long times_total(const cpu_times_t *t)
{
    return t->user + t->nice + t->system + t->idle +
           t->iowait + t->irq + t->softirq + t->steal;
}

int read_cpu_snapshot(cpu_snapshot_t *s)
{
    memset(s, 0, sizeof *s);

    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL)
        return -1;

    char line[512];
    while (fgets(line, sizeof line, fp)) {
        if (strncmp(line, "cpu", 3) == 0) {
            cpu_times_t t = { 0 };
            char label[16];

            int n = sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                           label, &t.user, &t.nice, &t.system, &t.idle,
                           &t.iowait, &t.irq, &t.softirq, &t.steal);
            if (n < 5)
                continue;                    

            if (strcmp(label, "cpu") == 0) {
                s->total = t;                  
            } else {
                int idx = atoi(label + 3);
                if (idx >= 0 && idx < MAX_CPUS) {
                    s->cpu[idx] = t;
                    if (idx + 1 > s->ncpu)
                        s->ncpu = idx + 1;
                }
            }
        } else if (sscanf(line, "procs_running %lu", &s->procs_running) == 1) {
            continue;
        } else if (sscanf(line, "procs_blocked %lu", &s->procs_blocked) == 1) {
            continue;
        } else if (sscanf(line, "processes %lu", &s->forks_since_boot) == 1) {
            continue;
        }
    }
    fclose(fp);

    return s->ncpu > 0 ? 0 : -1;
}

double cpu_util_percent(const cpu_times_t *a, const cpu_times_t *b)
{
    unsigned long long dt = times_total(b) - times_total(a);
    unsigned long long di = times_idle(b)  - times_idle(a);

    if (dt == 0 || di > dt)
        return 0.0;

    return 100.0 * (double)(dt - di) / (double)dt;
}

static bool read_first_line(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return false;

    bool ok = fgets(buf, (int)cap, fp) != NULL;
    fclose(fp);
    if (ok)
        buf[strcspn(buf, "\n")] = '\0';
    return ok;
}

int read_thermal_zones(thermal_zone_t *zones, int max)
{
    DIR *d = opendir("/sys/class/thermal");
    if (d == NULL)
        return 0;

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (strncmp(e->d_name, "thermal_zone", 12) != 0)
            continue;

        char path[512], buf[64];

        snprintf(path, sizeof path, "/sys/class/thermal/%s/temp", e->d_name);
        if (!read_first_line(path, buf, sizeof buf))
            continue;
        zones[n].celsius = atol(buf) / 1000.0;

        snprintf(path, sizeof path, "/sys/class/thermal/%s/type", e->d_name);
        if (!read_first_line(path, zones[n].type, sizeof zones[n].type))
            snprintf(zones[n].type, sizeof zones[n].type, "%.31s", e->d_name);

        n++;
    }
    closedir(d);
    return n;
}

static double pct(unsigned long long part, unsigned long long whole)
{
    return whole ? 100.0 * (double)part / (double)whole : 0.0;
}

cmd_status_t cmd_cpu(int argc, char **argv)
{
    long sample = 500;

    if (argc > 1 && (!parse_long(argv[1], &sample) || sample < 50 || sample > 10000)) {
        fprintf(stderr, "usage: cpu [sample_ms 50..10000]\n");
        return CMD_ERROR;
    }

    cpu_snapshot_t *before = malloc(sizeof *before);
    cpu_snapshot_t *after  = malloc(sizeof *after);
    if (before == NULL || after == NULL || read_cpu_snapshot(before) != 0) {
        fprintf(stderr, "cpu: cannot read /proc/stat\n");
        free(before);
        free(after);
        return CMD_ERROR;
    }

    sleep_ms(sample);
    read_cpu_snapshot(after);

    printf("%s== CPU over %ld ms (%d cores) ==%s\n",
           clr(ANSI_BOLD), sample, after->ncpu, clr(ANSI_RESET));
    printf("  %-5s %6s %6s %6s  %s\n", "CPU", "usr%", "sys%", "idle%", "busy");

    /* i = -1 is the whole machine, then one row per core */
    for (int i = -1; i < after->ncpu; i++) {
        const cpu_times_t *x = (i < 0) ? &before->total : &before->cpu[i];
        const cpu_times_t *y = (i < 0) ? &after->total  : &after->cpu[i];

        unsigned long long dt = times_total(y) - times_total(x);
        double busy = cpu_util_percent(x, y);

        char label[16];
        if (i < 0)
            snprintf(label, sizeof label, "all");
        else
            snprintf(label, sizeof label, "cpu%d", i);

        printf("  %-5s %6.1f %6.1f %6.1f  ", label,
               pct((y->user + y->nice) - (x->user + x->nice), dt),
               pct((y->system + y->irq + y->softirq) - (x->system + x->irq + x->softirq), dt),
               pct(y->idle - x->idle, dt));
        print_bar(stdout, busy / 100.0, 16);
        printf(" %5.1f%%\n", busy);
    }

    printf("  Tasks now: %lu running, %lu waiting on disk; %lu processes created since boot\n",
           after->procs_running, after->procs_blocked, after->forks_since_boot);

    thermal_zone_t zones[MAX_THERMAL];
    int nz = read_thermal_zones(zones, MAX_THERMAL);
    if (nz == 0) {
        printf("  Thermal : no sensors in /sys/class/thermal (normal in a VM or WSL)\n");
    } else {
        for (int i = 0; i < nz; i++) {
            const char *c = zones[i].celsius >= 85 ? ANSI_RED
                          : zones[i].celsius >= 70 ? ANSI_YELLOW : ANSI_GREEN;
            printf("  Thermal : %-20s %s%5.1f °C%s\n",
                   zones[i].type, clr(c), zones[i].celsius, clr(ANSI_RESET));
        }
    }

    free(before);
    free(after);
    return CMD_OK;
}
