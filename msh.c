/*
 * msh - a minimal Unix shell
 * ICS 2305: Systems Programming
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 128
#define DELIMS " \t\r\n"

static int last_status = 0;     /* status of the previous command */

/* ---------- background job bookkeeping ---------- */

#define MAX_BG 64

static volatile pid_t bg_pid[MAX_BG];          /* 0 = empty slot            */
static volatile sig_atomic_t bg_done[MAX_BG];  /* 1 = finished, not printed */
static volatile int bg_status[MAX_BG];

/* Runs when a child changes state. It only waits for PIDs we know are
 * background jobs, so it can never steal the status of a foreground
 * command that run_external() is waiting for. */
static void on_sigchld(int sig)
{
    int saved = errno;
    (void)sig;

    for (int i = 0; i < MAX_BG; i++) {
        if (bg_pid[i] > 0 && !bg_done[i]) {
            int st;
            if (waitpid(bg_pid[i], &st, WNOHANG) > 0) {
                bg_status[i] = st;
                bg_done[i] = 1;
            }
        }
    }
    errno = saved;
}

static void install_sigchld_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;       /* restart getline/waitpid if interrupted */
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        perror("sigaction");
}

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
