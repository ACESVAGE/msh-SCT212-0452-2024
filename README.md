# msh

Name: Brooklyn Bruce Kidiga
Student ID: SCT212-0452/2024

A small Unix shell written in C for ICS 2305 Systems Programming (Question 1).

It reads a line, splits it into arguments and runs the command in a child process, using fork, execvp and waitpid directly.

## Building and running

    git clone git@github.com:ACESVAGE/msh-SCT212-0452-2024.git
    cd msh-SCT212-0452-2024
    make
    ./msh

`make clean` removes the binary. It builds with -Wall -Wextra and gives no warnings. To run my test cases from a script use `sh tests/run_tests.sh`.

## What it does

All eight tasks are done.

1. Prompt and input loop. Lines are read with getline so any length works, and Ctrl-D exits cleanly.
2. Lines are split on spaces and tabs with strtok. Blank lines are ignored.
3. External commands run with fork, execvp and waitpid. An unknown command prints an error and the shell carries on.
4. Built-ins: cd (no argument goes to $HOME), help, and exit with an optional status.
5. The prompt shows the status of the last command, like msh[0]>. A command killed by a signal gives 128 plus the signal number.
6. A trailing & runs the command in the background and prints its PID. Finished jobs are collected so there are no zombies.
7. `cmd > file` sends output to a file, creating or truncating it with mode 0644.
8. Ctrl-C stops the foreground command but not the shell.

I did not go beyond the brief, so there are no pipes, no `<` and no `>>`.

## How it works

Every command goes through the same steps. The shell reads the line with getline, splits it with strtok and then forks. The child does all the setup: it puts Ctrl-C back to its default so the command can actually be killed, and if the line had a > it opens the file and uses dup2 to point stdout at it. Then execvp replaces the child with the program. If execvp ever returns it has failed, so the child prints an error and calls _exit with 127 for command not found. I used _exit rather than exit so the child doesn't flush output buffers it copied from the shell. The parent calls waitpid on that one child and turns the status into a number, the exit code for a normal exit or 128 plus the signal number if it was killed. That number goes in the prompt, which is why a Ctrl-C'd sleep shows 130.

cd and exit have to be built-ins because they change the shell itself. A working directory belongs to a process, so a cd run as a child would change its own directory and die, and the shell would stay where it was. Exit run as a child would only end the child.

Background jobs were the hard part. A finished child stays in the process table as a zombie until its parent waits for it. Foreground commands are waited for straight away, but a background job ends whenever it likes, so I keep a small table of background PIDs and a SIGCHLD handler that calls waitpid with WNOHANG on those PIDs only. That way it can never take the exit status of a foreground command. SIGCHLD is blocked around fork until the PID is saved, otherwise a very quick job could finish before I know about it.

For Ctrl-C, the terminal sends SIGINT to the whole foreground group, shell included. The shell has a handler that only prints a newline, installed without SA_RESTART so getline returns and the prompt is redrawn. Background jobs get their own process group with setpgid so they don't die with it.

## Limitations

- Only `>` is supported. There is no `>>`, `<`, `2>` or pipes.
- There is no quoting, variable expansion or wildcards, because strtok only splits on whitespace. So `echo "a   b"` is split at the spaces.
- It tracks at most 64 background jobs at once and takes at most 127 arguments per command.
- `cd -` does not work.
- There is no job control (fg, bg, jobs) and Ctrl-Z is not handled.

## Tests

The test plan, with what I expected and what I got, is in tests/test_plan.md. The same cases can be run with tests/run_tests.sh.

## Sources and AI declaration

- Linux man pages for fork, execve, waitpid, dup2, sigaction, getline and strtok.
- ICS 2305 course notes on processes and signals.
- I used Claude, an AI assistant, to help plan the project and to draft the code, the test plan and this README, including the first draft of the explanation above. I built and tested the shell myself and went through the code so I could explain it. 
