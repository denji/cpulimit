# Upgrading from the original cpulimit (0.2.x, 0.3.2)

Log lines, exit statuses and the options `-p -e -l -c -i -m -v -z` are unchanged, so existing
scripts keep working. These are the differences that a script or an operator can notice.

| Area | Original | 1.x |
|---|---|---|
| Option values | `atoi`: `-l 5x` and `-l abc` were accepted, `-m` could crash | strict parsing with ranges; `-c 0` and `-c 1000000` or more are rejected instead of spinning or failing in `nanosleep` |
| Command mode | a separate limiter process that could outlive its parent | one process; the exit status is the command's |
| Two limiters, one target | allowed; they undo each other's `SIGSTOP`/`SIGCONT` | the second is refused (exit 1) and names the first; `--no-lock` restores the old behavior |
| Process identity | pid only: a reused pid was limited as if it were the target | pid and start time |
| Signals | handled in the handler; a signal could be lost before the wait | flag-only handler; signals are blocked except inside `pselect()` |
| Clock | wall clock | `CLOCK_MONOTONIC` |
| New options | none | `-d`, `-s`, `-P`, `--no-lock`, `--no-boost`, `-V` and long options for all |
| Build | recursive Makefiles, Travis CI | one Makefile, GitHub Actions |

Control accuracy is the same: both versions use one controller, and both under-deliver at low
limits and overshoot near 50 %. The gain is a lower cost of the limiter itself
([BENCHMARKS](VERIFICATION.md#benchmarks)).
