#include "minios.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NRES 2
static const char RES_NAME[NRES] = { 'A', 'B' };

typedef struct {
    pthread_mutex_t res[NRES];
    pthread_mutex_t graph_lock;
    int             holder[NRES];
    int             waiting[3];
    atomic_int      victim;
    atomic_int      rollbacks;
    atomic_long     retries;
    double          t0;
} world_t;

typedef struct {
    world_t *w;
    int      id;
    int      first, second;
    char     mode;
} actor_t;

static void logf_(world_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void logf_(world_t *w, const char *fmt, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("  [%6.0f ms] %s\n", now_ms() - w->t0, msg);
}

static void set_state(world_t *w, int id, int waits_for, int acquired)
{
    pthread_mutex_lock(&w->graph_lock);
    w->waiting[id] = waits_for;
    if (acquired >= 0)
        w->holder[acquired] = id;
    pthread_mutex_unlock(&w->graph_lock);
}

static void release_res(world_t *w, int id, int r)
{
    pthread_mutex_lock(&w->graph_lock);
    if (w->holder[r] == id)
        w->holder[r] = 0;
    pthread_mutex_unlock(&w->graph_lock);
    pthread_mutex_unlock(&w->res[r]);
}

static void *actor_main(void *p)
{
    block_signals_in_thread();
    actor_t *a = p;
    world_t *w = a->w;
    int first = a->first, second = a->second;
    unsigned seed = (unsigned)(a->id * 7919) ^ (unsigned)now_ms();

    if (a->mode == 'o' && first > second) {
        int t = first;
        first = second;
        second = t;
    }

    for (;;) {
        set_state(w, a->id, first, -1);
        pthread_mutex_lock(&w->res[first]);
        set_state(w, a->id, -1, first);
        logf_(w, "T%d holds %c", a->id, RES_NAME[first]);
        sleep_ms(150);

        if (a->mode == 't') {
            if (pthread_mutex_trylock(&w->res[second]) != 0) {
                atomic_fetch_add(&w->retries, 1);
                logf_(w, "T%d: %c busy -> releases %c and backs off", a->id, RES_NAME[second], RES_NAME[first]);
                release_res(w, a->id, first);
                sleep_ms(20 + (long)(rand_r(&seed) % 120));
                continue;
            }
            set_state(w, a->id, -1, second);
        } else {
            set_state(w, a->id, second, -1);
            logf_(w, "T%d waits for %c", a->id, RES_NAME[second]);
            bool rolled_back = false;
            for (;;) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100 * 1000000L;
                if (ts.tv_nsec >= 1000000000L) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000L;
                }
                int rc = pthread_mutex_timedlock(&w->res[second], &ts);
                if (rc == 0)
                    break;
                if (rc == ETIMEDOUT && atomic_load(&w->victim) == a->id) {
                    logf_(w, "T%d is the victim: releases %c (rollback)", a->id, RES_NAME[first]);
                    set_state(w, a->id, -1, -1);
                    release_res(w, a->id, first);
                    atomic_fetch_add(&w->rollbacks, 1);
                    atomic_store(&w->victim, 0);
                    sleep_ms(200);
                    rolled_back = true;
                    break;
                }
            }
            if (rolled_back)
                continue;
            set_state(w, a->id, -1, second);
        }

        logf_(w, "T%d holds %c and %c -> working", a->id, RES_NAME[first], RES_NAME[second]);
        sleep_ms(50);
        release_res(w, a->id, second);
        release_res(w, a->id, first);
        logf_(w, "T%d done", a->id);
        return NULL;
    }
}

typedef struct {
    world_t    *w;
    atomic_bool stop;
    int         detections;
} detector_t;

static void *detector_main(void *p)
{
    block_signals_in_thread();
    detector_t *d = p;
    world_t *w = d->w;
    int streak = 0;
    while (!atomic_load(&d->stop)) {
        sleep_ms(100);
        pthread_mutex_lock(&w->graph_lock);
        int r1 = w->waiting[1], r2 = w->waiting[2];
        bool cycle = r1 >= 0 && r2 >= 0 && w->holder[r1] == 2 && w->holder[r2] == 1;
        pthread_mutex_unlock(&w->graph_lock);
        streak = cycle ? streak + 1 : 0;
        if (streak == 3 && atomic_load(&w->victim) == 0) {
            d->detections++;
            logf_(w, "%sDETECTOR: cycle T1 -> %c -> T2 -> %c -> T1 (deadlock)%s", clr(ANSI_RED), RES_NAME[r1],
                  RES_NAME[r2], clr(ANSI_RESET));
            logf_(w, "DETECTOR: victim = T2 (youngest); asking it to roll back");
            atomic_store(&w->victim, 2);
            streak = 0;
        }
    }
    return NULL;
}

static void run_world(char mode, const char *title)
{
    world_t w;
    memset(&w, 0, sizeof w);
    for (int i = 0; i < NRES; i++)
        pthread_mutex_init(&w.res[i], NULL);
    pthread_mutex_init(&w.graph_lock, NULL);
    w.waiting[0] = w.waiting[1] = w.waiting[2] = -1;
    atomic_init(&w.victim, 0);
    atomic_init(&w.rollbacks, 0);
    atomic_init(&w.retries, 0);
    w.t0 = now_ms();

    printf("\n%s-- %s --%s\n", clr(ANSI_BOLD), title, clr(ANSI_RESET));
    actor_t a1 = { &w, 1, 0, 1, mode }, a2 = { &w, 2, 1, 0, mode };
    detector_t det = { .w = &w };
    atomic_init(&det.stop, false);
    pthread_t t1, t2, td;
    pthread_create(&td, NULL, detector_main, &det);
    pthread_create(&t1, NULL, actor_main, &a1);
    pthread_create(&t2, NULL, actor_main, &a2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    atomic_store(&det.stop, true);
    pthread_join(td, NULL);

    printf("  result: finished in %.0f ms | deadlocks detected %d | rollbacks %d | trylock retries %ld\n",
           now_ms() - w.t0, det.detections, atomic_load(&w.rollbacks), atomic_load(&w.retries));
    for (int i = 0; i < NRES; i++)
        pthread_mutex_destroy(&w.res[i]);
    pthread_mutex_destroy(&w.graph_lock);
}

static pthread_mutex_t hang_a = PTHREAD_MUTEX_INITIALIZER, hang_b = PTHREAD_MUTEX_INITIALIZER;

static void *hang_t1(void *p)
{
    (void)p;
    pthread_mutex_lock(&hang_a);
    usleep(100000);
    pthread_mutex_lock(&hang_b);
    return NULL;
}

static void *hang_t2(void *p)
{
    (void)p;
    pthread_mutex_lock(&hang_b);
    usleep(100000);
    pthread_mutex_lock(&hang_a);
    return NULL;
}

int deadlock_child_main(void)
{
    pthread_t a, b;
    pthread_create(&a, NULL, hang_t1, NULL);
    pthread_create(&b, NULL, hang_t2, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}

static unsigned long long total_ticks(pid_t pid, int *threads, char *states, size_t cap)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/task", (int)pid);
    DIR *d = opendir(path);
    unsigned long long sum = 0;
    size_t k = 0;
    *threads = 0;
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char sp[300], buf[512];
        snprintf(sp, sizeof sp, "/proc/%d/task/%s/stat", (int)pid, e->d_name);
        FILE *fp = fopen(sp, "r");
        if (!fp)
            continue;
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        fclose(fp);
        buf[n] = '\0';
        char *rp = strrchr(buf, ')');
        char st;
        unsigned long long ut, stt;
        if (rp && sscanf(rp + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &st, &ut, &stt) == 3) {
            sum += ut + stt;
            if (k + 3 < cap) {
                k += (size_t)snprintf(states + k, cap - k, "%s%c", k ? "," : "", st);
            }
            (*threads)++;
        }
    }
    closedir(d);
    return sum;
}

static void run_hang(void)
{
    printf("\n%s-- hang: a REAL deadlock in a child process --%s\n", clr(ANSI_BOLD), clr(ANSI_RESET));
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        execl("/proc/self/exe", "minios", "--deadlock-child", (char *)NULL);
        _exit(127);
    }
    printf("  child %d: T1 locks A then B, T2 locks B then A\n", (int)pid);
    sleep_ms(500);
    int threads;
    char states[64] = "";
    unsigned long long before = total_ticks(pid, &threads, states, sizeof states);
    sleep_ms(1000);
    unsigned long long after = total_ticks(pid, &threads, states, sizeof states);
    int status;
    bool alive = waitpid(pid, &status, WNOHANG) == 0;
    printf("  after 1.5 s: alive=%s, %d threads in states [%s], CPU ticks used in last second = %llu\n",
           alive ? "yes" : "no", threads, states, after - before);
    if (alive && after == before) {
        printf("  %s-> alive, all threads sleeping, zero progress: deadlock suspected%s\n", clr(ANSI_RED),
               clr(ANSI_RESET));
        printf("     T1 --holds--> A      T1 ..waits..> B\n");
        printf("     T2 --holds--> B      T2 ..waits..> A      cycle: T1 -> B -> T2 -> A -> T1\n");
        printf("  recovery: kill the process (SIGKILL) — the OS reclaims both mutexes with it\n");
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    printf("  child reaped: %s\n", WIFSIGNALED(status) ? strsignal(WTERMSIG(status)) : "exited");
}

cmd_status_t cmd_deadlock_demo(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "all";
    bool all = strcmp(mode, "all") == 0;
    if (!all && strcmp(mode, "hang") && strcmp(mode, "detect") && strcmp(mode, "order") && strcmp(mode, "trylock")) {
        fprintf(stderr, "usage: deadlock_demo [hang|detect|order|trylock|all]\n");
        return CMD_ERROR;
    }
    printf("%sDeadlock needs: mutual exclusion + hold-and-wait + no preemption + circular wait%s\n",
           clr(ANSI_BOLD), clr(ANSI_RESET));
    if (all || strcmp(mode, "hang") == 0)
        run_hang();
    if (all || strcmp(mode, "detect") == 0)
        run_world('d', "detect: wait-for graph + victim rollback (breaks no-preemption)");
    if (all || strcmp(mode, "order") == 0)
        run_world('o', "order: always lock A before B (breaks circular wait)");
    if (all || strcmp(mode, "trylock") == 0)
        run_world('t', "trylock: never hold A while waiting for B (breaks hold-and-wait)");
    return CMD_OK;
}
