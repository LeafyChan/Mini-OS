#include "minios.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

#define TOP_MAX_PROCS 4096
#define TOP_SHOW      8

typedef struct {
    process_info_t info;
    double         cpu_pct;
} top_row_t;


static struct termios saved_tio;
static bool raw_mode = false;
static void term_raw(void)
{
    if (tcgetattr(STDIN_FILENO, &saved_tio) != 0)
        return;

    struct termios t = saved_tio;
    t.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0) {
        raw_mode = true;
        printf("\033[?1049h\033[?25l");
    }
}

static void term_restore(void)
{
    if (!raw_mode)
        return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    raw_mode = false;
}

static int cmp_rows(const void *a, const void *b)
{
    const top_row_t *x = a, *y = b;

    if (x->cpu_pct != y->cpu_pct)
        return x->cpu_pct < y->cpu_pct ? 1 : -1;

    return (x->info.rss_kb < y->info.rss_kb) - (x->info.rss_kb > y->info.rss_kb);
}

static unsigned long long prev_ticks_for(const process_info_t *prev, int nprev,
                                         pid_t pid, bool *found)
{
    for (int i = 0; i < nprev; i++) {
        if (prev[i].pid == pid) {
            *found = true;
            return prev[i].cpu_ticks;
        }
    }
    *found = false;
    return 0;
}

static void render(const cpu_snapshot_t *a, const cpu_snapshot_t *b,
                   process_info_t *prev, int nprev,
                   process_info_t *cur, int ncur,
                   top_row_t *rows, double interval, int frame, bool tty)
{
    if (tty)
        printf(ANSI_CLEAR);
    else
        printf("---- top frame %d ----\n", frame);

    struct utsname uts;
    double up = 0, idle = 0;
    char upbuf[32], clock[16];
    time_t now = time(NULL);

    strftime(clock, sizeof clock, "%H:%M:%S", localtime(&now));
    read_uptime(&up, &idle);
    fmt_duration(up, upbuf, sizeof upbuf);
    uname(&uts);

    load_info_t load = { 0 };
    read_loadavg(&load);

    printf("%sMiniOS top%s — %s — %s — up %s — load %.2f %.2f %.2f\n",
           clr(ANSI_BOLD), clr(ANSI_RESET), uts.nodename, clock, upbuf,
           load.load1, load.load5, load.load15);
    int states[128] = { 0 };
    long threads = 0;
    for (int i = 0; i < ncur; i++) {
        states[(unsigned char)cur[i].state & 127]++;
        threads += cur[i].threads;
    }
    printf("Tasks: %d processes (%ld threads) | %d running, %d sleeping, %d zombie\n",
           ncur, threads, states['R'], states['S'] + states['I'], states['Z']);
    double all = cpu_util_percent(&a->total, &b->total);
    printf("\nCPU all  ");
    print_bar(stdout, all / 100, 30);
    printf(" %5.1f%%\n", all);

    for (int i = 0; i < b->ncpu; i++) {
        double u = cpu_util_percent(&a->cpu[i], &b->cpu[i]);
        printf("  cpu%-3d ", i);
        print_bar(stdout, u / 100, 20);
        printf(" %5.1f%%%s", u, (i % 2 == 1 || i == b->ncpu - 1) ? "\n" : "   ");
    }
    mem_info_t m;
    if (read_meminfo(&m) == 0 && m.total_kb > 0) {
        double used = (double)(m.total_kb - m.available_kb);
        printf("Mem      ");
        print_bar(stdout, used / (double)m.total_kb, 30);
        printf(" %6.0f / %lu MB\n", used / 1024, m.total_kb / 1024);

        if (m.swap_total_kb > 0) {
            double su = (double)(m.swap_total_kb - m.swap_free_kb);
            printf("Swap     ");
            print_bar(stdout, su / (double)m.swap_total_kb, 30);
            printf(" %6.0f / %lu MB\n", su / 1024, m.swap_total_kb / 1024);
        }
    }

    thermal_zone_t z[MAX_THERMAL];
    int nz = read_thermal_zones(z, MAX_THERMAL);
    printf("Thermal  ");
    if (nz == 0)
        printf("no sensors here");
    for (int i = 0; i < nz && i < 4; i++)
        printf("%s %.1f°C  ", z[i].type, z[i].celsius);
    putchar('\n');
    unsigned long long dtotal = 0;
    {
        const cpu_times_t *x = &a->total, *y = &b->total;
        dtotal = (y->user + y->nice + y->system + y->idle +
                  y->iowait + y->irq + y->softirq + y->steal) -
                 (x->user + x->nice + x->system + x->idle +
                  x->iowait + x->irq + x->softirq + x->steal);
    }
    double per_cpu = (b->ncpu > 0) ? (double)dtotal / b->ncpu : 0;

    for (int i = 0; i < ncur; i++) {
        bool found;
        unsigned long long before = prev_ticks_for(prev, nprev, cur[i].pid, &found);

        rows[i].info = cur[i];
        rows[i].cpu_pct = (found && per_cpu > 0 && cur[i].cpu_ticks >= before)
                              ? 100.0 * (double)(cur[i].cpu_ticks - before) / per_cpu
                              : 0.0;
    }
    qsort(rows, (size_t)ncur, sizeof rows[0], cmp_rows);

    printf("\n%s%7s %7s %2s %6s %9s %4s  %-20s%s\n", clr(ANSI_CYAN),
           "PID", "PPID", "S", "%CPU", "RSS(KB)", "THR", "COMMAND", clr(ANSI_RESET));

    bool self_shown = false;
    for (int i = 0; i < ncur; i++) {
        const process_info_t *p = &rows[i].info;
        bool self = (p->pid == getpid());
        if (i >= TOP_SHOW && !(self && !self_shown))
            continue;
        self_shown |= self;

        printf("%7d %7d %2c %6.1f %9ld %4ld  %-20s%s\n",
               (int)p->pid, (int)p->ppid, p->state, rows[i].cpu_pct,
               p->rss_kb, p->threads, p->name, self ? "  <- MiniOS" : "");
    }

    printf("\n%s[q] quit  [+/-] refresh %.1fs%s\n", clr(ANSI_DIM), interval, clr(ANSI_RESET));
    fflush(stdout);                     /* without this the screen looks frozen */
}

cmd_status_t cmd_top(int argc, char **argv)
{
    bool tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    double interval = 2.0;
    long frames = tty ? 0 : 3;

    if (argc > 1 && (!parse_double(argv[1], &interval) || interval < 0.2 || interval > 60)) {
        fprintf(stderr, "usage: top [interval_s 0.2..60] [frames]\n");
        return CMD_ERROR;
    }
    if (argc > 2 && (!parse_long(argv[2], &frames) || frames < 0)) {
        fprintf(stderr, "usage: top [interval_s] [frames >= 0]\n");
        return CMD_ERROR;
    }

    cpu_snapshot_t *a = malloc(sizeof *a);
    cpu_snapshot_t *b = malloc(sizeof *b);
    process_info_t *prev = malloc(sizeof *prev * TOP_MAX_PROCS);
    process_info_t *cur  = malloc(sizeof *cur * TOP_MAX_PROCS);
    top_row_t      *rows = malloc(sizeof *rows * TOP_MAX_PROCS);

    if (!a || !b || !prev || !cur || !rows) {
        free(a); free(b); free(prev); free(cur); free(rows);
        return CMD_ERROR;
    }

    if (tty)
        term_raw();

    read_cpu_snapshot(a);
    int nprev = proc_list(prev, TOP_MAX_PROCS);
    sleep_ms(300);

    for (int frame = 1; !minios_interrupted; frame++) {
        read_cpu_snapshot(b);
        int ncur = proc_list(cur, TOP_MAX_PROCS);

        render(a, b, prev, nprev, cur, ncur, rows, interval, frame, tty);

        *a = *b;
        memcpy(prev, cur, sizeof *cur * (size_t)ncur);
        nprev = ncur;

        if (frames > 0 && frame >= frames)
            break;

        bool quit = false;
        double deadline = now_ms() + interval * 1000;

        while (!quit && !minios_interrupted) {
            double left = deadline - now_ms();
            if (left <= 0)
                break;

            if (!tty) {
                sleep_ms((long)left);
                break;
            }

            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(STDIN_FILENO, &rf);
            struct timeval tv = { (time_t)(left / 1000),
                                  (suseconds_t)((long)left % 1000) * 1000 };

            int r = select(STDIN_FILENO + 1, &rf, NULL, NULL, &tv);

            if (r < 0 && errno == EINTR)
                continue;

            if (r > 0) {
                char ch;
                if (read(STDIN_FILENO, &ch, 1) == 1) {
                    if (ch == 'q' || ch == 'Q')
                        quit = true;
                    else if (ch == '+')
                        interval = (interval + 0.5 > 60) ? 60 : interval + 0.5;
                    else if (ch == '-')
                        interval = (interval - 0.5 < 0.5) ? 0.5 : interval - 0.5;

                    if (ch == '+' || ch == '-')
                        break;
                }
            }
        }

        if (quit)
            break;
    }

    term_restore();
    minios_interrupted = 0;

    free(a); free(b); free(prev); free(cur); free(rows);
    return CMD_OK;
}
