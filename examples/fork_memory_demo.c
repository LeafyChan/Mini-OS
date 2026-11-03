/* Handbook 4.3 — the 'two kitchens' experiment (verbatim).
 * The "weird" part: both lines can show the SAME address with DIFFERENT
 * values. Addresses are virtual: each process has its own page table, and
 * copy-on-write gives the child a private copy the moment it writes.
 * (Also note the parent never waits: the child is a zombie until the
 * parent exits.) */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int secret = 42;

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        secret = 100;
        printf("Child:  secret=%d address=%p\n",
               secret, (void *)&secret);
    } else {
        sleep(1);
        printf("Parent: secret=%d address=%p\n",
               secret, (void *)&secret);
    }

    return 0;
}
