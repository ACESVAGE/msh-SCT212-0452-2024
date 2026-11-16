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

/* Looks for a trailing "&". If found, removes it from argv, and returns
 * the new argc. Sets *background to 1 or 0. Handles both "sleep 3 &" and
 * "sleep 3&". */
static int check_background(char **argv, int argc, int *background)
{
    *background = 0;
    if (argc == 0)
        return argc;

    char *last = argv[argc - 1];
    size_t len = strlen(last);

    if (len > 0 && last[len - 1] == '&') {
        *background = 1;
        if (len == 1)
            argv[--argc] = NULL;        /* "&" on its own: drop the token */
        else
            last[len - 1] = '\0';       /* "3&": cut the & off the word */
    }
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
    puts("other:");
    puts("  cmd &       run cmd in the background");
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
static int run_external(char **argv, int background)
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

    /* parent */
    if (background) {
        printf("[started pid %d]\n", (int)pid);
        return last_status;     /* starting a job does not change the status */
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    return status_to_code(status);
}

/* Collects any background jobs that have finished, so they do not stay
 * behind as zombies. WNOHANG means "do not wait if none are done yet". */
static void reap_background(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        printf("[done pid %d, status %d]\n", (int)pid, status_to_code(status));
}

int main(void)
{
    char *line = NULL;      /* getline allocates and grows this for us */
    size_t cap = 0;
    char *argv[MAX_ARGS];

    for (;;) {
        reap_background();

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

        int background;
        argc = check_background(argv, argc, &background);
        if (argc == 0) {
            fprintf(stderr, "msh: syntax error: missing command before '&'\n");
            continue;
        }

        if (strcmp(argv[0], "exit") == 0)
            break;
        else if (strcmp(argv[0], "cd") == 0)
            last_status = builtin_cd(argv);
        else if (strcmp(argv[0], "help") == 0)
            last_status = builtin_help();
        else
            last_status = run_external(argv, background);
    }

    free(line);
    return 0;
}
