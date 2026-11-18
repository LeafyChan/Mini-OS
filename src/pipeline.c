#include "minios.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char *argv[MAX_ARGS];
    char *in_file;
    char *out_file;
    bool  append;
} stage_t;

bool line_has_pipeline(const char *line)
{
    char quote = 0;
    for (const char *p = line; *p; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '|') {
            return true;
        }
    }
    return false;
}

static int split_stages(char *line, char **parts, int max)
{
    int n = 0;
    char quote = 0;
    parts[n++] = line;
    for (char *p = line; *p; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '|') {
            if (n >= max)
                return -1;
            *p = '\0';
            parts[n++] = p + 1;
        }
    }
    return n;
}

static int parse_stage(char *text, stage_t *st, bool first, bool last)
{
    char *tok[MAX_ARGS];
    int nt = split_args(text, tok, MAX_ARGS);
    if (nt <= 0)
        return -1;
    memset(st, 0, sizeof *st);
    int argc = 0, i = 0;
    if (strcmp(tok[0], "run") == 0)
        i = 1;
    for (; i < nt; i++) {
        if (strcmp(tok[i], "<") == 0 || strcmp(tok[i], ">") == 0 || strcmp(tok[i], ">>") == 0) {
            if (i + 1 >= nt) {
                fprintf(stderr, "pipeline: '%s' needs a file name\n", tok[i]);
                return -1;
            }
            if (tok[i][0] == '<') {
                if (!first) {
                    fprintf(stderr, "pipeline: '<' only allowed on the first command\n");
                    return -1;
                }
                st->in_file = tok[++i];
            } else {
                if (!last) {
                    fprintf(stderr, "pipeline: '>' only allowed on the last command\n");
                    return -1;
                }
                st->append = tok[i][1] == '>';
                st->out_file = tok[++i];
            }
        } else {
            st->argv[argc++] = tok[i];
        }
    }
    st->argv[argc] = NULL;
    return argc > 0 ? 0 : -1;
}

static void close_all(int pipes[][2], int count)
{
    for (int k = 0; k < count; k++) {
        if (pipes[k][0] >= 0) close(pipes[k][0]);
        if (pipes[k][1] >= 0) close(pipes[k][1]);
    }
}

int run_pipeline(char *line)
{
    char *parts[MAX_STAGES];
    int n = split_stages(line, parts, MAX_STAGES);
    if (n < 0) {
        fprintf(stderr, "pipeline: at most %d commands\n", MAX_STAGES);
        return -1;
    }
    stage_t stages[MAX_STAGES];
    for (int i = 0; i < n; i++) {
        if (parse_stage(parts[i], &stages[i], i == 0, i == n - 1) != 0) {
            fprintf(stderr, "pipeline: syntax error near command %d\n", i + 1);
            return -1;
        }
    }

    int pipes[MAX_STAGES][2];
    for (int i = 0; i < MAX_STAGES; i++)
        pipes[i][0] = pipes[i][1] = -1;
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            close_all(pipes, n - 1);
            return -1;
        }
    }

    pid_t pids[MAX_STAGES];
    int started = 0;
    fflush(NULL);
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            break;
        }
        if (pid == 0) {
            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < n - 1)
                dup2(pipes[i][1], STDOUT_FILENO);
            close_all(pipes, n - 1);

            if (stages[i].in_file) {
                int fd = open(stages[i].in_file, O_RDONLY);
                if (fd < 0) { perror(stages[i].in_file); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (stages[i].out_file) {
                int flags = O_WRONLY | O_CREAT | (stages[i].append ? O_APPEND : O_TRUNC);
                int fd = open(stages[i].out_file, flags, 0644);
                if (fd < 0) { perror(stages[i].out_file); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            execvp(stages[i].argv[0], stages[i].argv);
            fprintf(stderr, "pipeline: %s: %s\n", stages[i].argv[0], strerror(errno));
            _exit(127);
        }
        pids[started++] = pid;
    }

    close_all(pipes, n - 1);

    int last_status = -1, failures = 0;
    for (int i = 0; i < started; i++) {
        int status;
        while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR)
            ;
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (code != 0)
            failures++;
        if (i == n - 1)
            last_status = code;
    }
    if (started < n)
        return -1;
    if (failures > 0)
        fprintf(stderr, "[pipeline] %d of %d commands failed; last exit status %d\n", failures, n, last_status);
    return last_status;
}
