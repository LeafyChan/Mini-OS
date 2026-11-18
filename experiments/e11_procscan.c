#include "minios.h"

#include <stdlib.h>
#include <stdio.h>

#define ROUNDS 100
#define CAP    8192

int main(void)
{
    process_info_t *list = malloc(sizeof *list * CAP);
    if (list == NULL)
        return 1;

    int n = proc_list(list, CAP);

    double v[7];
    for (int r = 0; r < 7; r++) {
        double t0 = now_ms();
        for (int i = 0; i < ROUNDS; i++)
            proc_list(list, CAP);
        v[r] = (now_ms() - t0) / ROUNDS;
    }
    for (int i = 1; i < 7; i++)
        for (int j = i; j > 0 && v[j] < v[j - 1]; j--) {
            double t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
        }
    double per = v[3];

    printf("e11 processes on this machine      %8d\n", n);
    printf("e11 one full /proc scan            %8.3f ms\n", per);
    printf("e11 cost per process               %8.4f ms\n", n ? per / n : 0);
    printf("e11 scans possible per second      %8.0f\n", per > 0 ? 1000 / per : 0);
    printf("e11 CPU used by top at 2.0s refresh %7.3f %%\n", per / 2000.0 * 100);
    printf("e11 CPU used by top at 0.1s refresh %7.3f %%\n", per / 100.0 * 100);

    free(list);
    return 0;
}
