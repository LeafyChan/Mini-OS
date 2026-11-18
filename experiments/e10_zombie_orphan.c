#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void peek(pid_t pid, const char *label)
{
    char path[64], buf[512];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("e10   %-22s pid %d is gone from /proc\n", label, (int)pid);
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

    printf("e10   %-22s pid=%d state=%c ppid=%d%s\n",
           label, (int)pid, state, ppid,
           state == 'Z' ? "   <- ZOMBIE" : (ppid == 1 ? "   <- REPARENTED" : ""));
}

static void zombie_demo(void)
{
    puts("e10 zombie: child exits, parent deliberately does not wait()");

    fflush(NULL);
    pid_t p = fork();
    if (p == 0)
        _exit(0);

    usleep(200000);
    peek(p, "before wait()");

    waitpid(p, NULL, 0);
    usleep(50000);
    peek(p, "after wait()");
}

static void orphan_demo(void)
{
    puts("e10 orphan: parent exits first, child keeps running");

    fflush(NULL);
    pid_t outer = fork();
    if (outer == 0) {
        fflush(NULL);
        pid_t inner = fork();
        if (inner == 0) {
            usleep(400000);
            _exit(0);
        }
        printf("e10   grandchild pid=%d, its parent is %d\n",
               (int)inner, (int)getpid());
        FILE *f = fopen("/tmp/minios_e10_pid", "w");
        if (f) { fprintf(f, "%d\n", (int)inner); fclose(f); }
        fflush(stdout);
        _exit(0);
    }
    waitpid(outer, NULL, 0);

    usleep(150000);

    FILE *f = fopen("/tmp/minios_e10_pid", "r");
    int inner = -1;
    if (f) { if (fscanf(f, "%d", &inner) != 1) inner = -1; fclose(f); }
    if (inner > 0)
        peek((pid_t)inner, "grandchild now");
    remove("/tmp/minios_e10_pid");
}

int main(void)
{
    zombie_demo();
    putchar('\n');
    orphan_demo();
    return 0;
}
