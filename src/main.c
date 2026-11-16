#include "minios.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int run_commands(char *cmds)
{
    int last = CMD_OK;
    for (char *seg = strtok(cmds, ";"); seg != NULL; seg = strtok(NULL, ";")) {
        last = shell_execute_line(seg);
        fflush(stdout);
        if (last == CMD_EXIT)
            break;
    }
    return last == CMD_ERROR ? 1 : 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        puts("MiniOS " MINIOS_VERSION);
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        char line[MAX_LINE];
        snprintf(line, sizeof line, "%s", argv[2]);
        return run_commands(line);
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
        printf("MiniOS %s. Type help, or quit to leave.\n", MINIOS_VERSION);

    return shell_run(stdin, interactive) == CMD_ERROR ? 1 : 0;
}
