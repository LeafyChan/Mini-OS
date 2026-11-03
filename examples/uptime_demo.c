/* Handbook 3.1 — your first /proc reader (verbatim). */
#include <stdio.h>

int main(void)
{
    FILE *fp = fopen("/proc/uptime", "r");

    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    double uptime, idle;

    if (fscanf(fp, "%lf %lf", &uptime, &idle) != 2) {
        fprintf(stderr, "Could not read /proc/uptime\n");
        fclose(fp);
        return 1;
    }

    printf("System uptime: %.2f seconds\n", uptime);
    printf("Idle time: %.2f seconds\n", idle);

    fclose(fp);
    return 0;
}
