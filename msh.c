/*
 * msh - a minimal Unix shell
 * ICS 2305: Systems Programming
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *line = NULL;      /* getline allocates and grows this for us */
    size_t cap = 0;

    for (;;) {
        printf("msh> ");
        fflush(stdout);

        ssize_t n = getline(&line, &cap, stdin);
        if (n == -1) {          /* end of file: the user pressed Ctrl-D */
            putchar('\n');
            break;
        }

        /* nothing runs yet; later steps will use the line */
    }

    free(line);
    return 0;
}
