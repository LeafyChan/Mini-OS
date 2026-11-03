#include "minios.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static cmd_status_t cmd_help(int argc, char **argv);
static cmd_status_t cmd_quit(int argc, char **argv);

const command_t cmds_shell[] = {
    { "help",      cmd_help,      "help [command]",            "List commands, or show one command's usage" },
    { "system",    cmd_system,    "system",                    "Uptime, memory and load, read from /proc" },
    { "cpu",       cmd_cpu,       "cpu [sample_ms]",           "Per-core CPU usage and temperatures" },
    { "top",       cmd_top,       "top [interval_s] [frames]", "Live dashboard; press q to leave" },
    { "quit",      cmd_quit,      "quit",                      "Leave MiniOS (alias: exit)" },
    { "exit",      cmd_quit,      "exit",                      "Leave MiniOS" },
    { NULL, NULL, NULL, NULL }                  /* end marker */
};

static const command_t *const all_tables[] = {
    cmds_shell,
    cmds_process
    NULL
};

const command_t *shell_find_command(const char *name)
{
    for (int t = 0; all_tables[t]; t++)
        for (const command_t *c = all_tables[t]; c->name; c++)
            if (strcmp(c->name, name) == 0)
                return c;
    return NULL;
}

static cmd_status_t cmd_help(int argc, char **argv)
{
    if (argc > 1) {
        const command_t *c = shell_find_command(argv[1]);
        if (c == NULL) {
            fprintf(stderr, "help: no command '%s'\n", argv[1]);
            return CMD_ERROR;
        }
        printf("usage: %s\n  %s\n", c->usage, c->summary);
        return CMD_OK;
    }

    static const char *const owners[] = {
        "Module 1 — shell and system inspection (/proc)",
        "Module 2 — processes (fork, exec, wait)"
    };

    printf("%sMiniOS %s%s  —  %s\n", clr(ANSI_BOLD), MINIOS_VERSION, clr(ANSI_RESET), MINIOS_TEAM);

    for (int t = 0; all_tables[t]; t++) {
        printf("\n%s%s%s\n", clr(ANSI_CYAN), owners[t], clr(ANSI_RESET));
        for (const command_t *c = all_tables[t]; c->name; c++)
            printf("  %-30s %s\n", c->usage, c->summary);
    }
    return CMD_OK;
}

static cmd_status_t cmd_quit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return CMD_EXIT;
}

cmd_status_t shell_execute_line(char *line)
{
    line = str_trim(line);

    if (*line == '\0' || *line == '#')
        return CMD_OK;

    char *argv[MAX_ARGS];
    int argc = split_args(line, argv, MAX_ARGS);

    if (argc < 0) {
        fprintf(stderr, "error: too many arguments (max %d)\n", MAX_ARGS - 1);
        return CMD_ERROR;
    }
    if (argc == 0)
        return CMD_OK;

    const command_t *c = shell_find_command(argv[0]);
    if (c == NULL) {
        fprintf(stderr, "Unknown command '%s'. Type 'help'.\n", argv[0]);
        return CMD_ERROR;
    }

    return c->handler(argc, argv);
}

static void on_sigint(int sig)
{
    (void)sig;
    minios_interrupted = 1;
}

int shell_run(FILE *in, bool interactive)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    char buf[MAX_LINE];
    int last = CMD_OK;
    bool echo = !interactive;

    for (;;) {
        if (interactive) {
            printf("%s%s%s", clr(ANSI_BOLD), MINIOS_PROMPT, clr(ANSI_RESET));
            fflush(stdout);
        }

        minios_interrupted = 0;
        errno = 0;

        if (fgets(buf, sizeof buf, in) == NULL) {
            if (ferror(in) && errno == EINTR) { 
                clearerr(in);
                putchar('\n');
                continue;
            }
            break;
        }

        if (strchr(buf, '\n') == NULL && !feof(in)) {
            int ch;
            while ((ch = fgetc(in)) != EOF && ch != '\n')
                ;
            fprintf(stderr, "error: line longer than %d characters ignored\n", MAX_LINE - 1);
            continue;
        }

        char *line = str_trim(buf);
        if (echo && *line)
            printf("%s%s\n", MINIOS_PROMPT, line);

        cmd_status_t st = shell_execute_line(line);
        fflush(stdout);

        if (st == CMD_EXIT)
            break;
        last = st;
    }

    if (interactive)
        printf("Bye.\n");
    return last;
}
