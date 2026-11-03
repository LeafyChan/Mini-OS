#include "minios.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

char *str_trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    return s;
}

int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while (*p) {
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            break;

        if (argc >= max - 1) {
            argv[0] = NULL;
            return -1;
        }

        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p;
            p++;
        }

        argv[argc++] = p;

        if (quote)
            while (*p && *p != quote)
                p++;
        else
            while (*p && !isspace((unsigned char)*p))
                p++;

        if (*p)
            *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

bool parse_long(const char *s, long *out)
{
    if (s == NULL || *s == '\0')
        return false;

    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);

    if (errno != 0 || *end != '\0')
        return false;

    *out = v;
    return true;
}

bool parse_double(const char *s, double *out)
{
    if (s == NULL || *s == '\0')
        return false;

    char *end;
    errno = 0;
    double v = strtod(s, &end);

    if (errno != 0 || *end != '\0' || !isfinite(v))
        return false;

    *out = v;
    return true;
}

bool color_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
    return cached == 1;
}

const char *clr(const char *ansi)
{
    return color_enabled() ? ansi : "";
}
void print_bar(FILE *out, double fraction, int width)
{
    if (fraction < 0)
        fraction = 0;
    if (fraction > 1)
        fraction = 1;

    int filled = (int)lround(fraction * width);

    const char *colour = fraction < 0.60 ? ANSI_GREEN
                       : fraction < 0.85 ? ANSI_YELLOW
                                         : ANSI_RED;

    fputc('[', out);
    fputs(clr(colour), out);
    for (int i = 0; i < width; i++)
        fputc(i < filled ? '#' : '.', out);
    fputs(clr(ANSI_RESET), out);
    fputc(']', out);
}

void fmt_duration(double seconds, char *buf, size_t cap)
{
    long s = (long)seconds;
    long days  = s / 86400;
    long hours = (s % 86400) / 3600;
    long mins  = (s % 3600) / 60;
    long secs  = s % 60;

    if (days > 0)
        snprintf(buf, cap, "%ldd %02ld:%02ld:%02ld", days, hours, mins, secs);
    else
        snprintf(buf, cap, "%02ld:%02ld:%02ld", hours, mins, secs);
}

void sleep_ms(long ms)
{
    struct timespec req = { ms / 1000, (ms % 1000) * 1000000L }, rem;

    while (nanosleep(&req, &rem) != 0 && errno == EINTR) {
        if (minios_interrupted)
            return;
        req = rem;
    }
}

volatile sig_atomic_t minios_interrupted = 0;
