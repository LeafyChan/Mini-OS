/* Handbook 4.1 — fork() (verbatim). */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main(void)
{
    printf("Before fork\n");

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0)
        printf("I am the child. PID=%d\n", getpid());
    else
        printf("I am the parent. Child PID=%d\n", pid);

    return 0;
}
