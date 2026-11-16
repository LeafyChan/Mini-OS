/* ipc.c: pipe messages (handbook 5.3). owner m3
 *
 * A pipe is a BYTE STREAM: it does not remember where one write() ended.
 * Two messages written back-to-back can arrive as one read(), and one large
 * message can arrive in pieces. So every message is framed:
 *
 *     [ uint32 length ][ length bytes of text ]
 *
 * The header and body go out in ONE write() when they fit in PIPE_BUF
 * (4096 bytes on Linux): POSIX guarantees such writes are atomic, so
 * several workers sharing one pipe cannot interleave their messages.
 */
#include "minios.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IPC_MAX_MESSAGE (16u * 1024 * 1024)

ssize_t write_all(int fd, const void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, (const char *)buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += (size_t)w;
    }
    return (ssize_t)done;
}

/* Returns bytes read; less than n only at end-of-file. */
ssize_t read_all(int fd, void *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, (char *)buf + done, n - done);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;                       /* every write end is closed */
        done += (size_t)r;
    }
    return (ssize_t)done;
}

int send_result(int fd, const char *result)
{
    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t len = strlen(result);
    if (len > IPC_MAX_MESSAGE) {
        errno = EMSGSIZE;
        return -1;
    }

    /* TODO 1: write the result (header + body, one write when possible) */
    uint32_t hdr = (uint32_t)len;
    size_t total = sizeof hdr + len;
    char small[PIPE_BUF];
    char *frame = total <= sizeof small ? small : malloc(total);
    if (frame == NULL)
        return -1;
    memcpy(frame, &hdr, sizeof hdr);
    memcpy(frame + sizeof hdr, result, len);
    ssize_t w = write_all(fd, frame, total);
    if (frame != small)
        free(frame);
    return w == (ssize_t)total ? 0 : -1;
}

int receive_result(int fd, char *buf, size_t capacity)
{
    if (buf == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }

    /* TODO 2: read the result, header first, then the body */
    uint32_t len;
    ssize_t r = read_all(fd, &len, sizeof len);
    if (r == 0)
        return IPC_EOF;                  /* sender closed: no more messages */
    if (r != (ssize_t)sizeof len)
        return -1;                       /* truncated header */
    if (len > IPC_MAX_MESSAGE) {
        errno = EPROTO;
        return -1;
    }

    /* TODO 3: make sure the received data is safe to use
     *   - never write past 'capacity'
     *   - always NUL-terminate
     *   - drain any bytes we could not keep so the NEXT message still lines up */
    size_t keep = len < capacity - 1 ? len : capacity - 1;
    if (read_all(fd, buf, keep) != (ssize_t)keep)
        return -1;
    buf[keep] = '\0';

    char scratch[512];
    size_t skip = len - keep;
    while (skip > 0) {
        size_t chunk = skip < sizeof scratch ? skip : sizeof scratch;
        if (read_all(fd, scratch, chunk) != (ssize_t)chunk)
            return -1;
        skip -= chunk;
    }
    return (int)keep;
}

/* job_result_t travels as text: "id|value|pid|worker|ms|text" */
int send_job_result(int fd, const job_result_t *r)
{
    char msg[RESULT_MAX + 128];
    snprintf(msg, sizeof msg, "%d|%ld|%d|%d|%.6f|%s", r->job_id, r->value, (int)r->worker_pid, r->worker_id,
             r->elapsed_ms, r->text);
    return send_result(fd, msg);
}

int receive_job_result(int fd, job_result_t *r)
{
    char msg[RESULT_MAX + 128];
    int n = receive_result(fd, msg, sizeof msg);
    if (n < 0)
        return n;
    int pid, consumed = 0;
    memset(r, 0, sizeof *r);
    if (sscanf(msg, "%d|%ld|%d|%d|%lf|%n", &r->job_id, &r->value, &pid, &r->worker_id, &r->elapsed_ms,
               &consumed) != 5 || consumed == 0) {
        errno = EPROTO;
        return -1;
    }
    r->worker_pid = pid;
    snprintf(r->text, sizeof r->text, "%s", msg + consumed);
    return 0;
}
