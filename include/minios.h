#ifndef MINIOS_H
#define MINIOS_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#define MINIOS_VERSION "2.0-m1m2"
#define MINIOS_TEAM    "Kernel._.Crew"
#define MINIOS_PROMPT  "MiniOS> "
#define MAX_LINE       1024
#define MAX_ARGS       64
#define MAX_CPUS       256
#define MAX_THERMAL    32
#define PROC_NAME_LEN  32

#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_DIM    "\033[2m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_CLEAR  "\033[H\033[2J"
typedef enum { CMD_OK = 0, CMD_ERROR = 1, CMD_EXIT = 2 } cmd_status_t;
typedef cmd_status_t (*cmd_handler_t)(int argc, char **argv);

typedef struct {
    const char   *name;
    cmd_handler_t handler;
    const char   *usage;
    const char   *summary;
} command_t;

extern const command_t cmds_shell[];
extern const command_t cmds_process[];

typedef struct {
    unsigned long total_kb, free_kb, available_kb, buffers_kb, cached_kb;
    unsigned long swap_total_kb, swap_free_kb;
} mem_info_t;

typedef struct {
    double load1, load5, load15;
    int    runnable, entities, last_pid;
} load_info_t;

typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_times_t;

typedef struct {
    int           ncpu;
    cpu_times_t   total;
    cpu_times_t   cpu[MAX_CPUS];
    unsigned long procs_running, procs_blocked, forks_since_boot;
} cpu_snapshot_t;

typedef struct {
    char   type[32];
    double celsius;
} thermal_zone_t;

typedef struct {
    pid_t              pid, ppid;
    char               state;
    char               name[PROC_NAME_LEN];
    long               threads;
    long               rss_kb;
    unsigned long long cpu_ticks;
} process_info_t;

double      now_ms(void);
char       *str_trim(char *s);
int         split_args(char *line, char **argv, int max);
bool        parse_long(const char *s, long *out);
bool        parse_double(const char *s, double *out);
bool        color_enabled(void);
const char *clr(const char *ansi);
void        print_bar(FILE *out, double fraction, int width);
void        fmt_duration(double seconds, char *buf, size_t cap);
void        sleep_ms(long ms);
extern volatile sig_atomic_t minios_interrupted;

int              shell_run(FILE *in, bool interactive);
cmd_status_t     shell_execute_line(char *line);
const command_t *shell_find_command(const char *name);

int          read_uptime(double *uptime_s, double *idle_s);
int          read_meminfo(mem_info_t *m);
int          read_loadavg(load_info_t *l);
void         show_system_info(void);
cmd_status_t cmd_system(int argc, char **argv);

int          read_cpu_snapshot(cpu_snapshot_t *s);
double       cpu_util_percent(const cpu_times_t *before, const cpu_times_t *after);
int          read_thermal_zones(thermal_zone_t *zones, int max);
cmd_status_t cmd_cpu(int argc, char **argv);
cmd_status_t cmd_top(int argc, char **argv);

int          run_program(char **args);
int          proc_read(pid_t pid, process_info_t *out);
int          proc_list(process_info_t *out, int max);
const char  *proc_state_name(char s);
cmd_status_t cmd_run(int argc, char **argv);
cmd_status_t cmd_processes(int argc, char **argv);

#endif
