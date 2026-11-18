#include "minios.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static cmd_status_t cmd_help(int argc, char **argv);
static cmd_status_t cmd_quit(int argc, char **argv);

const command_t cmds_shell[] = {
    { "help",      cmd_help,      "help [command]",            "list commands, or show the usage of one command" },
    { "system",    cmd_system,    "system",                    "uptime, memory and load from /proc" },
    { "cpu",       cmd_cpu,       "cpu [sample_ms]",           "cpu use per core, and temperatures" },
    { "top",       cmd_top,       "top [interval_s] [frames]", "live dashboard, press q to leave" },
    { "quit",      cmd_quit,      "quit",                      "leave MiniOS" },
    { "exit",      cmd_quit,      "exit",                      "leave MiniOS" },
    { NULL, NULL, NULL, NULL }
};

static const command_t *const all_tables[] = {
    cmds_shell,
    cmds_process,
    cmds_ipc,
    cmds_threads,
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
        "Module 1: shell and system inspection",
        "Module 2: processes",
        "Module 3: pipes, jobs and pipelines",
        "Module 4: threads and synchronization"
    };

    printf("%sMiniOS %s%s\n", clr(ANSI_BOLD), MINIOS_VERSION, clr(ANSI_RESET));

    for (int t = 0; all_tables[t]; t++) {
        printf("\n%s%s%s\n", clr(ANSI_CYAN), owners[t], clr(ANSI_RESET));
        for (const command_t *c = all_tables[t]; c->name; c++)
            printf("  %-42s %s\n", c->usage, c->summary);
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

    if (line_has_pipeline(line))
        return run_pipeline(line) == 0 ? CMD_OK : CMD_ERROR;

    bool background = false;
    size_t len = strlen(line);
    if (line[len - 1] == '&') {
        background = true;
        line[len - 1] = '\0';
        line = str_trim(line);
    }

    char cmdline[256];
    snprintf(cmdline, sizeof cmdline, "%s", line);

    char *argv[MAX_ARGS];
    int argc = split_args(line, argv, MAX_ARGS);

    if (argc < 0) {
        fprintf(stderr, "error: too many arguments (max %d)\n", MAX_ARGS - 1);
        return CMD_ERROR;
    }
    if (argc == 0)
        return CMD_OK;

    if (background) {
        char **args = argv;
        if (strcmp(args[0], "run") == 0)
            args++;
        if (*args == NULL) {
            fprintf(stderr, "usage: run <program> [args...] &\n");
            return CMD_ERROR;
        }
        return run_background(args, cmdline) > 0 ? CMD_OK : CMD_ERROR;
    }

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
        bg_report_finished();
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
