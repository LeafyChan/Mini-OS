#include "minios.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define BANNER_W 48
static void banner_line(const char *text)
{
    printf("|  %-*s|\n", BANNER_W - 3, text);
}

static void banner_rule(void)
{
    putchar('+');
    for (int i = 0; i < BANNER_W; i++)
        putchar('-');
    puts("+");
}

static void banner(void)
{
    printf("%s", clr(ANSI_BOLD));
    banner_rule();
    banner_line("MiniOS " MINIOS_VERSION "   -   " MINIOS_TEAM);
    banner_rule();
    banner_line("Module 1   system, cpu, top      Anvitha");
    banner_line("Module 2   run, processes        Srujana");
    banner_rule();
    printf("%s", clr(ANSI_RESET));
    puts("Type 'help' to see the commands, 'quit' to leave.\n");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        puts("MiniOS " MINIOS_VERSION "  -  " MINIOS_TEAM);
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        char line[MAX_LINE];
        snprintf(line, sizeof line, "%s", argv[2]);
        return shell_execute_line(line) == CMD_ERROR ? 1 : 0;
    }

    if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            fprintf(stderr, "minios: cannot open %s\n", argv[1]);
            return 1;
        }
        int rc = shell_run(fp, false);
        fclose(fp);
        return rc == CMD_ERROR ? 1 : 0;
    }

    bool interactive = isatty(STDIN_FILENO);
    if (interactive)
        banner();

    return shell_run(stdin, interactive) == CMD_ERROR ? 1 : 0;
}
