# Test plan for msh

Tested on: YOUR OWN SETUP HERE (e.g. Ubuntu on WSL2, and the first line of `gcc --version`)

The commands can be typed by hand at the `./msh` prompt, or run with `sh tests/run_tests.sh`.

## Tests from the brief

| # | Input | Expected | Actual result |
|---|-------|----------|---------------|
| 1 | (blank line) | New prompt, no error | New prompt printed, nothing else. Status stayed 0. |
| 2 | `echo hello world` | Prints text, status 0 | Printed `hello world`. Next prompt was `msh[0]>`. |
| 3 | `ls /nonexistent` | Error from ls, non-zero status | `ls: cannot access '/nonexistent': No such file or directory`. Next prompt was `msh[2]>`. |
| 4 | `nosuchcommand` | Error message, status 127 | `msh: nosuchcommand: command not found`. Next prompt was `msh[127]>`. |
| 5 | `cd /tmp` then `pwd` | Prints /tmp | Printed `/tmp`. |
| 6 | `sleep 3 &` | Prompt returns at once, PID shown, no zombie later | `[started pid N]` printed and the prompt came back immediately. While the job ran, `ps` showed `sleep 3` as a child of msh. After it finished, with the shell idle at the prompt, `ps` showed no children and no defunct process. Pressing Enter then printed `[done pid N, status 0]`. |
| 7 | `echo hi > f.txt` then `cat f.txt` | File contains hi | Nothing printed for the echo. `cat` printed `hi`. |
| 8 | `sleep 100` then Ctrl-C | Sleep stops, shell survives, status 130 | Sleep stopped, the next prompt was `msh[130]>`, and the shell ran the next command (`echo still alive`) normally. |
| 9 | Ctrl-D at the prompt | Shell exits cleanly | Printed a newline and exited with code 0. |

## My own tests

| # | Input | Expected | Actual result |
|---|-------|----------|---------------|
| A | `cd` (no argument) then `pwd` | Goes to $HOME | Printed my home directory. |
| B | `echo one two three > g.txt` then `echo x > g.txt` then `cat g.txt` | File is truncated, contains only x | `cat` printed `x`. |
| C | `   echo    a <tab>  b   ` | Extra spaces and tabs ignored | Printed `a b`. |
| D | `exit 7`, then `echo $?` in the outer shell | Shell exits with 7 | Outer shell saw 7. |
| E | `cd /no/such/dir` | Error message, status 1, shell keeps running | `cd: /no/such/dir: No such file or directory`. Next prompt was `msh[1]>`. |

## Memory check

I built with `-fsanitize=address,undefined` and ran several commands, including a blank line, a failing command, `cd` and a background job, then ended the shell. AddressSanitizer and its leak checker printed no reports.
