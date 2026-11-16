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
