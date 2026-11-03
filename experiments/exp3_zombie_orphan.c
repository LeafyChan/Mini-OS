/* exp3 — Two things that go wrong when the parent misbehaves.
 *
 * ZOMBIE : child finishes, parent never calls wait().
 *          The child is dead but the kernel must keep its exit
 *          status somewhere until somebody asks. State = Z.
 *
 * ORPHAN : parent finishes first.
 *          The child is still running but has no parent.
 *          Linux hands it to init/systemd. PPID becomes 1.
 *
 * Both are read back out of /proc, so this also proves M1's
 * /proc reading and M2's process handling are talking.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* read the state letter and ppid straight out of /proc/<pid>/stat */
static void peek(pid_t pid, const char *label)
{
    char path[64], buf[512];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("EXP3   %-22s pid %d is gone from /proc\n", label, (int)pid);
        return;
    }
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    char *rp = strrchr(buf, ')');
    char  state = '?';
    int   ppid = -1;
    if (rp)
        sscanf(rp + 2, "%c %d", &state, &ppid);

    printf("EXP3   %-22s pid=%d state=%c ppid=%d%s\n",
           label, (int)pid, state, ppid,
           state == 'Z' ? "   <- ZOMBIE" : (ppid == 1 ? "   <- REPARENTED" : ""));
}

static void zombie_demo(void)
{
    puts("EXP3 zombie: child exits, parent deliberately does not wait()");

    pid_t p = fork();
    if (p == 0)
        _exit(0);                       /* child dies straight away */

    usleep(200000);                     /* give it time to die */
    peek(p, "before wait()");

    waitpid(p, NULL, 0);                /* now reap it */
    usleep(50000);
    peek(p, "after wait()");
}

static void orphan_demo(void)
{
    puts("EXP3 orphan: parent exits first, child keeps running");

    pid_t outer = fork();
    if (outer == 0) {
        /* this middle process makes a grandchild, then dies */
        pid_t inner = fork();
        if (inner == 0) {
            usleep(400000);             /* outlive my parent */
            _exit(0);
        }
        printf("EXP3   grandchild pid=%d, its parent is %d\n",
               (int)inner, (int)getpid());
        FILE *f = fopen("/tmp/minios_exp3_pid", "w");
        if (f) { fprintf(f, "%d\n", (int)inner); fclose(f); }
        _exit(0);                       /* parent dies, orphaning inner */
    }
    waitpid(outer, NULL, 0);

    usleep(150000);                     /* let the reparenting happen */

    FILE *f = fopen("/tmp/minios_exp3_pid", "r");
    int inner = -1;
    if (f) { if (fscanf(f, "%d", &inner) != 1) inner = -1; fclose(f); }
    if (inner > 0)
        peek((pid_t)inner, "grandchild now");
    remove("/tmp/minios_exp3_pid");
}

int main(void)
{
    zombie_demo();
    putchar('\n');
    orphan_demo();
    return 0;
}
