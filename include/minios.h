#ifndef MINIOS_H
#define MINIOS_H

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#define MINIOS_VERSION "3.0"
#define MINIOS_PROMPT  "MiniOS> "
#define MAX_LINE       1024
#define MAX_ARGS       64
#define MAX_STAGES     8
#define MAX_SUBMITTED  64
#define MAX_CPUS       256
#define MAX_THERMAL    32
#define PROC_NAME_LEN  32
#define QUEUE_CAPACITY 5
#define RESULT_MAX     256
#define NAME_LEN       64
#define MAX_BG_JOBS    32
#define POOL_MAX_THREADS 32

#define ANSI_RESET  "\033[0m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_DIM    "\033[2m"
#define ANSI_RED    "\033[31m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
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
extern const command_t cmds_ipc[];
extern const command_t cmds_threads[];

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

typedef enum { BG_FREE = 0, BG_RUNNING, BG_STOPPED, BG_DONE, BG_SIGNALED } bg_state_t;

typedef struct {
    int        id;
    pid_t      pid;
    bg_state_t state;
    int        exit_code;
    int        term_signal;
    double     start_ms;
    double     end_ms;
    bool       reported;
    char       cmdline[256];
} bg_job_t;

typedef struct {
    long cpu_seconds;
    long mem_mb;
    long max_files;
} exec_limits_t;

typedef enum {
    JOB_SUM = 0,
    JOB_PRIMES,
    JOB_FIB,
    JOB_HASH,
    JOB_SLEEP,
    JOB_STOP
} job_type_t;

typedef struct {
    int        id;
    job_type_t type;
    long       arg;
    int        priority;
    uint64_t   seq;
    double     submit_ms;
    char       name[NAME_LEN];
} job_t;

typedef struct {
    int    job_id;
    long   value;
    pid_t  worker_pid;
    int    worker_id;
    double elapsed_ms;
    char   text[RESULT_MAX];
} job_result_t;

typedef struct {
    const char *method;
    size_t      msgs;
    size_t      msg_bytes;
    double      total_ms;
    double      avg_rtt_us;
    double      mb_per_s;
    bool        verified;
} ipc_bench_result_t;

typedef struct {
    job_t      items[QUEUE_CAPACITY];
    int        in, out;
    sem_t      mutex;
    sem_t      empty;
    sem_t      full;
    atomic_int count;
    atomic_int blocked_producers;
    atomic_int blocked_consumers;
    atomic_long total_pushed;
    atomic_long total_popped;
} job_queue_t;

typedef struct {
    int           worker_id;
    pthread_t     thread;
    job_queue_t  *queue;
    atomic_bool   shutdown;
    bool          verbose;
    long          jobs_done;
    long          result_sum;
    double        busy_ms;
} worker_context_t;

typedef enum { RACE_UNSAFE = 0, RACE_SEMAPHORE, RACE_MUTEX, RACE_ATOMIC } race_mode_t;

typedef enum { POLICY_FIFO = 0, POLICY_PRIORITY } sched_policy_t;
typedef struct thread_pool thread_pool_t;

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
void        block_signals_in_thread(void);
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

void         bg_init(void);
int          run_background(char **args, const char *cmdline);
void         bg_report_finished(void);
bool         bg_find(int id_or_pid, bg_job_t *copy);
void         bg_cleanup(void);
cmd_status_t cmd_ps(int argc, char **argv);
cmd_status_t cmd_wait(int argc, char **argv);
cmd_status_t cmd_kill(int argc, char **argv);
int          run_limited(char **args, const exec_limits_t *lim);
int          apply_limits_to_pid(pid_t pid, const exec_limits_t *lim);
cmd_status_t cmd_runlimit(int argc, char **argv);
cmd_status_t cmd_limit(int argc, char **argv);

#define IPC_EOF (-2)
ssize_t write_all(int fd, const void *buf, size_t n);
ssize_t read_all(int fd, void *buf, size_t n);
int     send_result(int fd, const char *result);
int     receive_result(int fd, char *buf, size_t capacity);
int     send_job_result(int fd, const job_result_t *r);
int     receive_job_result(int fd, job_result_t *r);

int          job_next_id(void);
int          job_parse(int argc, char **argv, job_t *out);
long         job_execute(const job_t *job, char *text, size_t cap);
const char  *job_type_name(job_type_t t);
int          submit_job(const job_t *job, job_result_t *out);
cmd_status_t cmd_submit(int argc, char **argv);
cmd_status_t cmd_jobs(int argc, char **argv);
bool         line_has_pipeline(const char *line);
int          run_pipeline(char *line);
int          bench_pipe(size_t msgs, size_t bytes, ipc_bench_result_t *out);
int          bench_shm(size_t msgs, size_t bytes, ipc_bench_result_t *out);
cmd_status_t cmd_ipcbench(int argc, char **argv);

int          queue_init(job_queue_t *q);
void         queue_destroy(job_queue_t *q);
int          queue_push(job_queue_t *q, job_t job);
int          queue_pop(job_queue_t *q, job_t *job);
int          queue_snapshot(job_queue_t *q, bool occupied[QUEUE_CAPACITY],
                            int ids[QUEUE_CAPACITY]);
long         race_run(long iters, int threads, race_mode_t mode, double *ms);
cmd_status_t cmd_race(int argc, char **argv);
void        *worker_main(void *arg);
int          workers_run(int nthreads, int njobs, job_type_t type, long arg,
                         bool verbose, double *elapsed_ms);
double       workers_time(int nthreads, int njobs, job_type_t type, long arg);
cmd_status_t cmd_workers(int argc, char **argv);

thread_pool_t *pool_create(int nthreads, sched_policy_t policy);
int          pool_submit(thread_pool_t *p, job_t job);
int          pool_resize(thread_pool_t *p, int nthreads);
void         pool_set_policy(thread_pool_t *p, sched_policy_t policy);
void         pool_pause(thread_pool_t *p, bool paused);
int          pool_wait_idle(thread_pool_t *p);
size_t       pool_completed_order(thread_pool_t *p, int *ids, size_t max);
void         pool_print_status(thread_pool_t *p, FILE *out);
void         pool_destroy(thread_pool_t *p);
cmd_status_t cmd_pool(int argc, char **argv);
void         threads_cleanup(void);
void         viz_render_queue(FILE *out, job_queue_t *q, int producers_alive,
                              int consumers_alive, bool in_place);
cmd_status_t cmd_prodcons(int argc, char **argv);
cmd_status_t cmd_deadlock_demo(int argc, char **argv);
int          deadlock_child_main(void);

cmd_status_t cmd_compare(int argc, char **argv);

#endif
