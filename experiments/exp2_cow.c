/* exp2 — Does fork() copy the parent's memory?
 *
 * The handbook (4.3) says parent and child do not share ordinary
 * variables. The obvious guess is that fork COPIES everything.
 *
 * Test: give the parent 1 MB, then 256 MB, and time fork() each time.
 *
 *   If fork copies   -> 256 MB should take ~256x longer than 1 MB
 *   If fork is lazy  -> the time barely moves
 *
 * Whatever the numbers say is the answer. We are not assuming.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define ROUNDS 40

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Write one byte in every 4096-byte page.
 *
 * One byte per page is enough: the hardware protects memory a whole
 * page at a time, so touching any byte in a page copies that page.
 *
 * `volatile` matters. Without it gcc -O2 sees that nobody ever reads
 * the buffer afterwards, deletes the whole loop, and the experiment
 * silently measures nothing. (We hit exactly that on the first run.) */
static void touch_all(volatile char *p, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 4096)
        p[i] = (char)(i & 0xff);
}

/* Time fork+wait while the parent holds `mb` megabytes of real pages. */
static double fork_time_with(size_t mb, int child_writes)
{
    size_t bytes = mb * 1024 * 1024;
    char  *buf   = malloc(bytes);
    if (buf == NULL)
        return -1;

    touch_all(buf, bytes);              /* make the pages genuinely exist */

    double t0 = now_ms();
    for (int i = 0; i < ROUNDS; i++) {
        pid_t p = fork();
        if (p == 0) {
            if (child_writes)
                touch_all(buf, bytes);  /* force every page to be copied */
            _exit(0);
        }
        waitpid(p, NULL, 0);
    }
    double per = (now_ms() - t0) / ROUNDS;

    free(buf);
    return per;
}

int main(void)
{
    size_t sizes[] = { 1, 16, 64, 256 };

    fork_time_with(1, 0);               /* warm-up */

    puts("EXP2 child exits immediately (does not touch the memory):");
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        printf("EXP2   parent holds %4zu MB -> fork+wait %8.3f ms\n",
               sizes[i], fork_time_with(sizes[i], 0));

    puts("EXP2 child WRITES to all of it (forces the real copy):");
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        printf("EXP2   parent holds %4zu MB -> fork+wait %8.3f ms\n",
               sizes[i], fork_time_with(sizes[i], 1));

    return 0;
}
