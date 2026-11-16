/*
 * msh - a minimal Unix shell
 * ICS 2305: Systems Programming
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
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

/* Looks for "> file" or ">file" in argv. If found, removes it from argv,
 * sets *outfile, and returns the new argc. Returns -1 on a syntax error. */
static int check_redirect(char **argv, int argc, char **outfile)
{
    *outfile = NULL;

    for (int i = 0; i < argc; i++) {
        if (argv[i][0] != '>')
            continue;

        char *file = argv[i] + 1;       /* ">file" form */
        int remove = 1;

        if (*file == '\0') {            /* "> file" form */
            if (i + 1 >= argc) {
                fprintf(stderr, "msh: syntax error: expected a file after '>'\n");
                return -1;
            }
            file = argv[i + 1];
            remove = 2;
        }
        *outfile = file;

        /* shift the rest of argv left over the removed words */
        for (int j = i; j + remove <= argc; j++)
            argv[j] = argv[j + remove];
        argc -= remove;
        i--;
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
    puts("  cmd > file  send the output of cmd to file");
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
static int run_external(char **argv, int background, const char *outfile)
{
    /* Block SIGCHLD until the new pid is recorded. Otherwise a very short
     * job could finish before we know about it and stay a zombie. */
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        sigprocmask(SIG_SETMASK, &old, NULL);
        return 1;
    }

    if (pid == 0) {
        /* child: put the signal mask and handler back to normal */
        signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_SETMASK, &old, NULL);

        /* redirection is done here in the child, never in the parent,
         * so the shell's own stdout is not affected */
        if (outfile != NULL) {
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fprintf(stderr, "msh: %s: %s\n", outfile, strerror(errno));
                _exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                _exit(1);
            }
            close(fd);
        }

        execvp(argv[0], argv);

        /* execvp only returns if it failed */
        fprintf(stderr, "msh: %s: %s\n", argv[0],
                errno == ENOENT ? "command not found" : strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    /* parent */
    if (background) {
        for (int i = 0; i < MAX_BG; i++) {
            if (bg_pid[i] == 0) {
                bg_done[i] = 0;
                bg_pid[i] = pid;
                break;
            }
        }
        sigprocmask(SIG_SETMASK, &old, NULL);
        printf("[started pid %d]\n", (int)pid);
        return last_status;     /* starting a job does not change the status */
    }

    sigprocmask(SIG_SETMASK, &old, NULL);

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    return status_to_code(status);
}

/* Tells the user about background jobs the handler has already reaped. */
static void report_background(void)
{
    for (int i = 0; i < MAX_BG; i++) {
        if (bg_pid[i] > 0 && bg_done[i]) {
            printf("[done pid %d, status %d]\n", (int)bg_pid[i],
                   status_to_code(bg_status[i]));
            bg_pid[i] = 0;
            bg_done[i] = 0;
        }
    }
}

int main(void)
{
    char *line = NULL;      /* getline allocates and grows this for us */
    size_t cap = 0;
    char *argv[MAX_ARGS];

    install_sigchld_handler();

    for (;;) {
        report_background();

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

        char *outfile;
        argc = check_redirect(argv, argc, &outfile);
        if (argc == -1) {
            last_status = 2;
            continue;
        }
        if (argc == 0) {
            fprintf(stderr, "msh: syntax error: missing command before '>'\n");
            last_status = 2;
            continue;
        }

        if (strcmp(argv[0], "exit") == 0)
            break;
        else if (strcmp(argv[0], "cd") == 0)
            last_status = builtin_cd(argv);
        else if (strcmp(argv[0], "help") == 0)
            last_status = builtin_help();
        else
            last_status = run_external(argv, background, outfile);
    }

    free(line);
    return 0;
}
