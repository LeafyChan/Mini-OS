#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int fd[2];
    if (pipe(fd) == -1)
        return 1;

    if (fork() == 0) {
        close(fd[1]);
        usleep(200000);
        char buf[100];
        ssize_t n = read(fd[0], buf, sizeof buf - 1);
        buf[n] = '\0';
        printf("one read() returned %zd bytes: %s\n", n, buf);
        return 0;
    }

    close(fd[0]);
    write(fd[1], "first", 5);
    write(fd[1], "second", 6);
    close(fd[1]);
    wait(NULL);
    return 0;
}
