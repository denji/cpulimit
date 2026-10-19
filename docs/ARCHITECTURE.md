# cpulimit architecture and verification plan

This document is normative for the code under `src/`. A change that breaks a
rule below must change this document in the same commit.

## 1. What the program does

`cpulimit` throttles a target process (or a process and its descendants) to a
fraction of CPU time by alternating `SIGCONT` ("work slice") and `SIGSTOP`
("stop slice") inside a fixed chunk (default 100 ms). A feedback controller
adjusts the work fraction from the CPU time the targets really used.

## 2. Modules and dependencies

```
cpulimit.c  (main, control loop, signals)
  ├─ cli.c            argv -> struct cli_config     pure, no exit(), no globals
  ├─ limiter.c        controller arithmetic         pure, no I/O
  ├─ process_group.c  per-target process table      depends on the iterator + alloc seam
  │    └─ process_iterator_<os>.c   one file per platform, selected in process_iterator.c
  │         └─ procfs_parse.c       /proc/<pid>/stat parser   pure, no allocation
  ├─ lock.c           one limiter per target        fcntl record lock
  ├─ daemon.c         detach from the terminal
  ├─ log.c            stdout/stderr and syslog
  ├─ util.c           strict number parsing, monotonic time, interruptible wait
  ├─ list.c           intrusive list used by the table
  └─ alloc.h          allocation seam (tests inject failures)
```

Rules: modules marked *pure* never call `exit()`, never touch global state and
never allocate. Only `cpulimit.c` calls `exit()`. Only `process_iterator_*.c`
know about the operating system's process interface.

## 3. Process model

* **Command mode** (`cpulimit -l N cmd args`): ONE process besides the target.
  `main` forks the target, then runs the control loop itself. The target is its
  child, so its termination and exit status come from `waitpid()` rather than
  from polling, and there is no separate limiter process that could be orphaned.
  (Before: parent + limiter child + target; signalling only the parent left the
  limiter running unattended.)
* **Attach mode** (`-p`, `-e`): one process; with `--daemon` it detaches first.

Exit status in command mode is unchanged: the command's exit status, or the raw
wait status when it was killed by a signal.

## 4. Signals

Handled: `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`. The handler does exactly one
thing: store the signal number in a `volatile sig_atomic_t`. Everything else
(`SIGCONT` to the targets, cleanup, `printf`) runs in normal context.

These four signals are blocked except inside the wait, which uses `pselect()`
with the original mask, so a signal can neither be lost between the flag check
and the sleep nor delay shutdown by more than one wake-up. This is the POSIX
pattern for the race; `sleep()` and `nanosleep()` alone cannot close it.

`SIGPIPE` is ignored (a closed stdout must not kill the limiter while a target
is stopped).

### Invariant S1 (never strand a target)
Whenever the process leaves the control loop for ANY reason (target ended, stop
signal, allocation failure, error), every process in the group that cpulimit
may have stopped receives `SIGCONT` before `exit`.

Not provable in-process: `SIGKILL` of cpulimit, an OOM kill or a crash while the
target is in its stop slice leaves the target stopped. That is inherent to
SIGSTOP-based throttling and is documented in the README; `kill -CONT` recovers.

## 5. Control loop (one iteration)

1. `update_process_group()` -> measured CPU of each process.
2. `compute_slices(limit, pcpu, prev_rate, chunk)` -> (work, stop).
3. `SIGCONT` all; wait `work`; `SIGSTOP` all (skipping processes below the
   `-m` threshold); wait `stop`.

### Invariants of `compute_slices` (checked exhaustively on a grid, and by CBMC)
* L1 `0 <= work <= chunk` and `work + stop == chunk`.
* L2 `0 <= rate <= 1`, never NaN.
* L3 no floating-point exception is raised for any finite input.
* L4 where the previous formula was well defined, results agree to 1 ns.

## 6. Process table (`process_group`)

* A process is identified by **(pid, start time)**. A new process that reuses a
  pid is a new entry. (Before: `assert()` abort.)
* Each update stamps the entries it sees with a generation number; entries not
  stamped are freed (terminated processes). The table size is bounded by the
  number of live processes.
* The table owns the `struct process` objects; `proclist` only references them.
  `remove_process()` unlinks from both, so `proclist` never dangles.
* Every allocation is checked. On failure `update_process_group()` returns -1,
  the group stays valid and can be closed without leaks (tested by failing the
  k-th allocation for every k).

### `/proc/<pid>/stat` parsing
The command name (field 2) is arbitrary text. Numeric fields are located after
the **last** `)`. No `strtok`, `sscanf` or `strto*`; digits are accumulated with
an explicit overflow check. Parser input is untrusted (any local user chooses
process names), so it is fuzzed.

## 7. One limiter per target (`lock.c`)

Two limiters on one target fight each other (each one's `SIGCONT` undoes the
other's `SIGSTOP`). A limiter takes an exclusive `fcntl` record lock on
`<rundir>/<pid>-<starttime>.lock`. The kernel drops the lock when the holder
dies, so there are no stale locks. `rundir` is `$XDG_RUNTIME_DIR/cpulimit` or
`/tmp/cpulimit-<uid>`, created `0700` and verified (owner, mode, not a symlink)
before use. Keying on the start time means a recycled pid is a different target.

## 8. Verification plan

| Property | Technique | Where |
|---|---|---|
| Parsers reject/accept exactly the grammar | unit + property tests, libFuzzer + ASan/UBSan | `test/*_test.c`, `fuzz/` |
| Controller invariants L1-L4 | exhaustive grid + CBMC proof | `limiter_test.c`, `verify/` |
| `/proc` parser memory safety | libFuzzer, CBMC bounded proof | `fuzz/`, `verify/` |
| Memory safety / leaks / UB | ASan, UBSan, valgrind, allocation-failure injection | CI `sanitizers` |
| Process table behavior | scripted iterator (deterministic) | `group_test.c` |
| Real limiting accuracy, signal handling, S1 | integration tests with real processes | `test/*.sh` |
| Regressions of past defects | mutation check: each fixed defect reintroduced must fail a test | `test/mutation.sh` |
| Concurrency / races | stress under load (many processes, churn), TSan N/A (no threads) | `test/load_test.sh` |
| Performance | syscalls and CPU per cycle vs process count | `bench/` |
| Portability | CI matrix: OS x compiler x libc x arch | `.github/workflows/` |

Limits of this plan, stated up front: proofs are bounded (CBMC unwinds loops to
a stated depth) and cover pure modules only; the macOS, FreeBSD and Solaris
iterators are built in CI but their behavior is only exercised by the shared
iterator tests on those hosts.
