/*
 * msh - a minimal Unix shell
 * ICS 2305: Systems Programming
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 128
#define DELIMS " \t\r\n"

static int last_status = 0;     /* status of the previous command */

/* Splits line in place on spaces and tabs. Returns the number of
 * arguments and always NULL-terminates argv. */
static int parse_line(char *line, char **argv)
{
    int argc = 0;
    char *tok = strtok(line, DELIMS);

    while (tok != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = tok;
        tok = strtok(NULL, DELIMS);
    }
    argv[argc] = NULL;
    return argc;
}

/* cd [dir]: with no argument, go to $HOME. Returns 0 on success. */
static int builtin_cd(char **argv)
{
    const char *dir = argv[1];

    if (dir == NULL) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }
    }
    if (chdir(dir) == -1) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_help(void)
{
    puts("msh - a minimal shell");
    puts("built-ins:");
    puts("  cd [dir]    change directory (no argument goes to $HOME)");
    puts("  help        show this message");
    puts("  exit        leave the shell");
    return 0;
}

/* Turns a waitpid status into a single number:
 * normal exit -> the exit code, killed by a signal -> 128 + signal. */
static int status_to_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

/* Runs an external command and returns its status code. */
static int run_external(char **argv)
{
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* child: replace this process with the command */
        execvp(argv[0], argv);

        /* execvp only returns if it failed */
        fprintf(stderr, "msh: %s: %s\n", argv[0],
                errno == ENOENT ? "command not found" : strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    /* parent: wait for that one child to finish */
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    return status_to_code(status);
}

int main(void)
{
    char *line = NULL;      /* getline allocates and grows this for us */
    size_t cap = 0;
    char *argv[MAX_ARGS];

    for (;;) {
        printf("msh[%d]> ", last_status);
        fflush(stdout);

        ssize_t n = getline(&line, &cap, stdin);
        if (n == -1) {          /* end of file: the user pressed Ctrl-D */
            putchar('\n');
            break;
        }

        int argc = parse_line(line, argv);
        if (argc == 0)
            continue;           /* blank line: just show the prompt again */

        if (strcmp(argv[0], "exit") == 0)
            break;
        else if (strcmp(argv[0], "cd") == 0)
            last_status = builtin_cd(argv);
        else if (strcmp(argv[0], "help") == 0)
            last_status = builtin_help();
        else
            last_status = run_external(argv);
    }

    free(line);
    return 0;
}
