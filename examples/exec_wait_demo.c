/* Handbook 4.4 — fork + exec + waitpid (verbatim, plus 4.5 "print the exit status"). */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        execlp("date", "date", NULL);
        perror("execlp");
        exit(1);
    }

    printf("Parent: waiting for child...\n");

    int status;
    waitpid(pid, &status, 0);

    printf("Parent: child finished.\n");
    if (WIFEXITED(status))
        printf("Parent: child exit status = %d\n", WEXITSTATUS(status));
    return 0;
}
