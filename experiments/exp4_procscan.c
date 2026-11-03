/* exp4 — How expensive is MiniOS's own 'processes' command?
 *
 * This one links against the REAL project code: it calls proc_list()
 * out of M2's process.c. So it measures our implementation, not a
 * rewrite of it.
 *
 *   'top' calls proc_list() once per frame.
 *   At 2 second refresh that is fine.
 *   At 0.05 second refresh, is it still fine?  Measure, don't guess.
 */
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

    int n = proc_list(list, CAP);       /* warm-up + count */

    /* median of 7 batches, same reason as exp1: one scheduling hiccup
     * must not become the headline number */
    double v[7];
    for (int r = 0; r < 7; r++) {
        double t0 = now_ms();
        for (int i = 0; i < ROUNDS; i++)
            proc_list(list, CAP);
        v[r] = (now_ms() - t0) / ROUNDS;
    }
    for (int i = 1; i < 7; i++)         /* tiny insertion sort */
        for (int j = i; j > 0 && v[j] < v[j - 1]; j--) {
            double t = v[j]; v[j] = v[j - 1]; v[j - 1] = t;
        }
    double per = v[3];

    printf("EXP4 processes on this machine      %8d\n", n);
    printf("EXP4 one full /proc scan            %8.3f ms\n", per);
    printf("EXP4 cost per process               %8.4f ms\n", n ? per / n : 0);
    printf("EXP4 scans possible per second      %8.0f\n", per > 0 ? 1000 / per : 0);
    printf("EXP4 CPU used by top at 2.0s refresh %7.3f %%\n", per / 2000.0 * 100);
    printf("EXP4 CPU used by top at 0.1s refresh %7.3f %%\n", per / 100.0 * 100);

    free(list);
    return 0;
}
