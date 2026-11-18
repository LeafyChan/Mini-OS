#include "minios.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    job_queue_t *q;
    int          first_id, count;
    long         delay_ms;
} producer_arg_t;

typedef struct {
    job_queue_t *q;
    long         delay_ms;
    atomic_int  *seen;
    int          max_id;
    long         consumed;
} consumer_arg_t;

static atomic_int producers_alive;
static atomic_int consumers_alive;
static atomic_bool viz_stop;

void viz_render_queue(FILE *out, job_queue_t *q, int producers, int consumers, bool in_place)
{
    bool occ[QUEUE_CAPACITY];
    int ids[QUEUE_CAPACITY];
    int n = queue_snapshot(q, occ, ids);
    int empty_v = 0, full_v = 0;
    sem_getvalue(&q->empty, &empty_v);
    sem_getvalue(&q->full, &full_v);

    if (in_place)
        fputs("\r\033[K", out);
    fputs("Buffer ", out);
    for (int i = 0; i < QUEUE_CAPACITY; i++) {
        if (occ[i] && ids[i] < 0)
            fprintf(out, "%s[STOP]%s", clr(ANSI_RED), clr(ANSI_RESET));
        else if (occ[i])
            fprintf(out, "%s[#%-3d]%s", clr(ANSI_YELLOW), ids[i], clr(ANSI_RESET));
        else
            fputs("[    ]", out);
    }
    fprintf(out, " %d/%d | empty=%d full=%d | blocked P=%d C=%d | alive P=%d C=%d | in %ld out %ld", n,
            QUEUE_CAPACITY, empty_v, full_v, atomic_load(&q->blocked_producers),
            atomic_load(&q->blocked_consumers), producers, consumers, atomic_load(&q->total_pushed),
            atomic_load(&q->total_popped));
    if (!in_place)
        fputc('\n', out);
    fflush(out);
}

static void *producer(void *p)
{
    block_signals_in_thread();
    producer_arg_t *a = p;
    for (int i = 0; i < a->count; i++) {
        job_t j = { .id = a->first_id + i, .type = JOB_SUM, .arg = 1000 };
        queue_push(a->q, j);
        sleep_ms(a->delay_ms);
    }
    atomic_fetch_sub(&producers_alive, 1);
    return NULL;
}

static void *consumer(void *p)
{
    block_signals_in_thread();
    consumer_arg_t *a = p;
    for (;;) {
        job_t j;
        if (queue_pop(a->q, &j) != 0 || j.type == JOB_STOP)
            break;
        if (j.id >= 1 && j.id <= a->max_id)
            atomic_fetch_add(&a->seen[j.id], 1);
        a->consumed++;
        sleep_ms(a->delay_ms);
    }
    atomic_fetch_sub(&consumers_alive, 1);
    return NULL;
}

typedef struct {
    job_queue_t *q;
    int          P, C;
    bool         tty;
} monitor_arg_t;

static void *monitor(void *p)
{
    block_signals_in_thread();
    monitor_arg_t *m = p;
    char last[512] = "";
    int lines = 0;
    while (!atomic_load(&viz_stop)) {
        if (m->tty) {
            viz_render_queue(stdout, m->q, atomic_load(&producers_alive), atomic_load(&consumers_alive), true);
        } else if (lines < 60) {
            char buf[512];
            FILE *mem = fmemopen(buf, sizeof buf, "w");
            if (mem) {
                viz_render_queue(mem, m->q, atomic_load(&producers_alive), atomic_load(&consumers_alive), false);
                fclose(mem);
                buf[sizeof buf - 1] = '\0';
                char *cut = strstr(buf, " | in ");
                size_t keylen = cut ? (size_t)(cut - buf) : strlen(buf);
                if (strncmp(buf, last, keylen) != 0 || last[keylen] != '\0') {
                    fputs(buf, stdout);
                    memcpy(last, buf, keylen);
                    last[keylen] = '\0';
                    lines++;
                }
            }
        }
        sleep_ms(100);
    }
    return NULL;
}

cmd_status_t cmd_prodcons(int argc, char **argv)
{
    long P = 2, C = 2, items = 20, pms = 15, cms = 150;
    long *vals[] = { &P, &C, &items, &pms, &cms };
    for (int i = 1; i < argc && i <= 5; i++) {
        if (!parse_long(argv[i], vals[i - 1]) || *vals[i - 1] < 0) {
            fprintf(stderr, "usage: prodcons [producers] [consumers] [items] [produce_ms] [consume_ms]\n");
            return CMD_ERROR;
        }
    }
    if (P < 1 || C < 1 || P > 16 || C > 16 || items < 1 || items > 100000) {
        fprintf(stderr, "prodcons: 1..16 producers/consumers, 1..100000 items\n");
        return CMD_ERROR;
    }

    job_queue_t q;
    queue_init(&q);
    atomic_int *seen = calloc((size_t)items + 1, sizeof *seen);
    pthread_t pt[16], ct[16], mt;
    producer_arg_t pa[16];
    consumer_arg_t ca[16];
    if (!seen)
        return CMD_ERROR;

    printf("%s== Producer-consumer: %ld producers (every %ld ms), %ld consumers (every %ld ms), %ld items ==%s\n",
           clr(ANSI_BOLD), P, pms, C, cms, items, clr(ANSI_RESET));
    atomic_store(&producers_alive, (int)P);
    atomic_store(&consumers_alive, (int)C);
    atomic_store(&viz_stop, false);

    monitor_arg_t ma = { &q, (int)P, (int)C, isatty(STDOUT_FILENO) != 0 };
    pthread_create(&mt, NULL, monitor, &ma);

    for (long i = 0; i < C; i++) {
        ca[i] = (consumer_arg_t){ .q = &q, .delay_ms = cms, .seen = seen, .max_id = (int)items };
        pthread_create(&ct[i], NULL, consumer, &ca[i]);
    }
    long per = items / P, next = 1;
    for (long i = 0; i < P; i++) {
        long cnt = per + (i == P - 1 ? items % P : 0);
        pa[i] = (producer_arg_t){ .q = &q, .first_id = (int)next, .count = (int)cnt, .delay_ms = pms };
        next += cnt;
        pthread_create(&pt[i], NULL, producer, &pa[i]);
    }
    for (long i = 0; i < P; i++)
        pthread_join(pt[i], NULL);
    for (long i = 0; i < C; i++) {
        job_t pill = { .type = JOB_STOP };
        queue_push(&q, pill);
    }
    for (long i = 0; i < C; i++)
        pthread_join(ct[i], NULL);
    atomic_store(&viz_stop, true);
    pthread_join(mt, NULL);
    if (ma.tty)
        putchar('\n');

    long ok = 0, dup = 0, missing = 0;
    for (long id = 1; id <= items; id++) {
        int s = atomic_load(&seen[id]);
        ok += s == 1;
        dup += s > 1;
        missing += s == 0;
    }
    printf("Consumed per consumer:");
    for (long i = 0; i < C; i++)
        printf(" C%ld=%ld", i + 1, ca[i].consumed);
    printf("\nIntegrity: %ld/%ld items consumed exactly once, %ld duplicated, %ld missing -> %s\n", ok, items, dup,
           missing, (ok == items) ? "PASS" : "FAIL");
    free(seen);
    queue_destroy(&q);
    return ok == items ? CMD_OK : CMD_ERROR;
}
