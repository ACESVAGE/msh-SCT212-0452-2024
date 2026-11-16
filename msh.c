/*
 * msh - a minimal Unix shell
 * ICS 2305: Systems Programming
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 128
#define DELIMS " \t\r\n"

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

int main(void)
{
    char *line = NULL;      /* getline allocates and grows this for us */
    size_t cap = 0;
    char *argv[MAX_ARGS];

    for (;;) {
        printf("msh> ");
        fflush(stdout);

        ssize_t n = getline(&line, &cap, stdin);
        if (n == -1) {          /* end of file: the user pressed Ctrl-D */
            putchar('\n');
            break;
        }

        int argc = parse_line(line, argv);
        if (argc == 0)
            continue;           /* blank line: just show the prompt again */

        /* TEMPORARY: print what we parsed, to check it works */
        for (int i = 0; argv[i] != NULL; i++)
            printf("argv[%d] = \"%s\"\n", i, argv[i]);
    }

    free(line);
    return 0;
}
