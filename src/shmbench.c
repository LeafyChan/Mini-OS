/* shmbench.c: pipe against posix shared memory. owner m3
 *
 * Both methods do the same work per message: the parent hands S bytes to a
 * child, the child copies them into its own buffer, checksums them, and
 * acknowledges. We time the full round trip.
 *
 *   pipe : parent write() -> kernel buffer -> child read()   (2 copies)
 *          ack comes back on a second pipe
 *   shm  : parent memcpy() into a shared page -> child reads it directly
 *          (1 copy); two process-shared semaphores signal "ready"/"done"
 *
 * Shared memory avoids the kernel copy but needs its own synchronization;
 * that trade-off is the point of the experiment.
 */
#include "minios.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    sem_t    ready;       /* parent -> child: message in place      */
    sem_t    done;        /* child -> parent: message processed     */
    uint64_t len;
    uint64_t checksum;
    int      stop;
    char     data[];      /* flexible array member                  */
} shm_channel_t;

static uint64_t fnv1a(const char *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++)
        h = (h ^ (unsigned char)p[i]) * 1099511628211ULL;
    return h;
}

static void fill_pattern(char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (char)('A' + (i * 7) % 26);
}

static int sem_wait_retry(sem_t *s)
{
    while (sem_wait(s) != 0)
        if (errno != EINTR)
            return -1;
    return 0;
}

static void finish(ipc_bench_result_t *out, const char *method, size_t msgs, size_t bytes, double ms, bool ok)
{
    out->method = method;
    out->msgs = msgs;
    out->msg_bytes = bytes;
    out->total_ms = ms;
    out->avg_rtt_us = msgs ? ms * 1000.0 / (double)msgs : 0;
    out->mb_per_s = ms > 0 ? ((double)msgs * (double)bytes / (1024.0 * 1024.0)) / (ms / 1000.0) : 0;
    out->verified = ok;
}

int bench_pipe(size_t msgs, size_t bytes, ipc_bench_result_t *out)
{
    int data[2], ack[2];
    if (pipe(data) != 0)
        return -1;
    if (pipe(ack) != 0) {
        close(data[0]);
        close(data[1]);
        return -1;
    }
    char *buf = malloc(bytes);
    if (!buf)
        return -1;
    fill_pattern(buf, bytes);
    uint64_t expected = fnv1a(buf, bytes);

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        free(buf);
        return -1;
    }
    if (pid == 0) {
        close(data[1]);
        close(ack[0]);
        char *rx = malloc(bytes ? bytes : 1);
        uint64_t len;
        while (rx && read_all(data[0], &len, sizeof len) == (ssize_t)sizeof len && len > 0) {
            if (read_all(data[0], rx, len) != (ssize_t)len)
                break;
            uint64_t sum = fnv1a(rx, len);
            if (write_all(ack[1], &sum, sizeof sum) != (ssize_t)sizeof sum)
                break;
        }
        free(rx);
        _exit(0);
    }
    close(data[0]);
    close(ack[1]);

    bool ok = true;
    uint64_t len = bytes;
    double t0 = now_ms();
    for (size_t i = 0; i < msgs; i++) {
        uint64_t sum = 0;
        if (write_all(data[1], &len, sizeof len) < 0 || write_all(data[1], buf, bytes) < 0 ||
            read_all(ack[0], &sum, sizeof sum) != (ssize_t)sizeof sum) {
            ok = false;
            break;
        }
        if (sum != expected)
            ok = false;
    }
    double ms = now_ms() - t0;

    uint64_t zero = 0;
    write_all(data[1], &zero, sizeof zero);          /* stop marker */
    close(data[1]);
    close(ack[0]);
    waitpid(pid, NULL, 0);
    free(buf);
    finish(out, "pipe", msgs, bytes, ms, ok);
    return ok ? 0 : -1;
}

int bench_shm(size_t msgs, size_t bytes, ipc_bench_result_t *out)
{
    char name[64];
    snprintf(name, sizeof name, "/minios_bench_%d", (int)getpid());
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        return -1;
    }
    size_t size = sizeof(shm_channel_t) + bytes;
    if (ftruncate(fd, (off_t)size) != 0) {
        perror("ftruncate");
        close(fd);
        shm_unlink(name);
        return -1;
    }
    shm_channel_t *ch = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);            /* the mapping keeps the object alive            */
    shm_unlink(name);     /* remove the name now: nothing leaks on a crash */
    if (ch == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    sem_init(&ch->ready, 1, 0);   /* pshared = 1: usable across fork() */
    sem_init(&ch->done, 1, 0);
    ch->stop = 0;

    char *buf = malloc(bytes);
    if (!buf) {
        munmap(ch, size);
        return -1;
    }
    fill_pattern(buf, bytes);
    uint64_t expected = fnv1a(buf, bytes);

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        free(buf);
        munmap(ch, size);
        return -1;
    }
    if (pid == 0) {
        char *rx = malloc(bytes ? bytes : 1);
        while (rx && sem_wait_retry(&ch->ready) == 0 && !ch->stop) {
            memcpy(rx, ch->data, ch->len);         /* same work as the pipe reader */
            ch->checksum = fnv1a(rx, ch->len);
            sem_post(&ch->done);
        }
        free(rx);
        _exit(0);
    }

    bool ok = true;
    double t0 = now_ms();
    for (size_t i = 0; i < msgs; i++) {
        memcpy(ch->data, buf, bytes);
        ch->len = bytes;
        sem_post(&ch->ready);
        if (sem_wait_retry(&ch->done) != 0) {
            ok = false;
            break;
        }
        if (ch->checksum != expected)
            ok = false;
    }
    double ms = now_ms() - t0;

    ch->stop = 1;
    sem_post(&ch->ready);
    waitpid(pid, NULL, 0);
    sem_destroy(&ch->ready);
    sem_destroy(&ch->done);
    munmap(ch, size);
    free(buf);
    finish(out, "shm", msgs, bytes, ms, ok);
    return ok ? 0 : -1;
}

static void print_row(const ipc_bench_result_t *r)
{
    printf("  %-5s %8zu %10zu %10.1f %12.1f %10.1f  %s\n", r->method, r->msgs, r->msg_bytes, r->total_ms,
           r->avg_rtt_us, r->mb_per_s, r->verified ? "ok" : "CHECKSUM FAIL");
}

static int run_pair(size_t msgs, size_t bytes)
{
    ipc_bench_result_t p, s;
    int rc = bench_pipe(msgs, bytes, &p);
    rc |= bench_shm(msgs, bytes, &s);
    print_row(&p);
    print_row(&s);
    if (s.total_ms > 0)
        printf("  %-5s shm is %.2fx %s than pipe for %zu-byte messages\n", "", p.total_ms / s.total_ms,
               p.total_ms >= s.total_ms ? "faster" : "slower", bytes);
    return rc;
}

cmd_status_t cmd_ipcbench(int argc, char **argv)
{
    printf("%s  %-5s %8s %10s %10s %12s %10s  %s%s\n", clr(ANSI_CYAN), "IPC", "MSGS", "BYTES", "TOTAL ms",
           "RTT us/msg", "MB/s", "CHECK", clr(ANSI_RESET));
    int rc = 0;
    if (argc > 1 && strcmp(argv[1], "sweep") == 0) {
        static const size_t sizes[] = { 64, 4096, 65536, 1 << 20 };
        static const size_t counts[] = { 20000, 10000, 2000, 200 };
        for (size_t i = 0; i < 4; i++)
            rc |= run_pair(counts[i], sizes[i]);
    } else {
        long msgs = 5000, bytes = 4096;
        if ((argc > 1 && (!parse_long(argv[1], &msgs) || msgs <= 0 || msgs > 10000000)) ||
            (argc > 2 && (!parse_long(argv[2], &bytes) || bytes <= 0 || bytes > (64L << 20)))) {
            fprintf(stderr, "usage: ipcbench [msgs] [bytes<=64MB] | ipcbench sweep\n");
            return CMD_ERROR;
        }
        rc = run_pair((size_t)msgs, (size_t)bytes);
    }
    return rc == 0 ? CMD_OK : CMD_ERROR;
}
