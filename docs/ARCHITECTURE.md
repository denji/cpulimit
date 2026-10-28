# cpulimit architecture

This document is normative for the code under `src/`. A change that breaks a
rule below must change this document in the same commit.

## 1. What the program does

`cpulimit` throttles a target process (or a process and its descendants) to a
fraction of CPU time by alternating `SIGCONT` ("work slice") and `SIGSTOP`
("stop slice") inside a fixed chunk (default 100 ms). A feedback controller
adjusts the work fraction from the CPU time the targets really used.

## 2. Modules and dependencies

```
src/main.c  (main, control loop, signals)
  ├─ cli/cli.c               argv -> struct cli_config     pure, no exit(), no globals
  ├─ core/limiter.c          controller arithmetic         pure, no I/O
  ├─ core/process_group.c    per-target process table      depends on the iterator + alloc seam
  │    └─ platform/iterator.c  selects one of platform/{linux,macos,freebsd,openbsd,netbsd,solaris}/iterator.c
  │         ├─ platform/snapshot.c      the target and its descendants, shared by every snapshot iterator
  │         └─ platform/procfs_parse.c   /proc/<pid>/stat parser   pure, no allocation
  ├─ runtime/lock.c          one limiter per target        fcntl record lock
  ├─ runtime/daemon.c        detach from the terminal
  ├─ runtime/log.c           stdout/stderr and syslog
  ├─ util/util.c             strict number parsing, monotonic time, interruptible wait
  ├─ core/list.c             intrusive list used by the table
  └─ core/alloc.h            allocation seam (tests inject failures)

Adding a platform is one new file, `src/platform/<os>/iterator.c`, plus a branch in
`src/platform/iterator.c`; nothing else includes platform code. DragonFly BSD, Haiku and
GNU/Hurd have no iterator and are not built. OpenBSD and NetBSD share `platform/bsd_sysctl.h`.
```

Rules: modules marked *pure* never call `exit()`, never touch global state and
never allocate. Only `src/main.c` calls `exit()`. Only `src/platform/*/iterator.c`
know about the operating system's process interface.

## 3. Process model

* **Command mode** (`cpulimit -l N cmd args`): ONE process besides the target.
  `main` forks the target, then runs the control loop itself. The target is its
  child, so its termination and exit status come from `waitpid()` rather than
  from polling, and there is no separate limiter process that could be orphaned.
  (The original ran a parent, a limiter child and the target; signalling only the parent
  left the limiter running unattended.)
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
SIGSTOP-based throttling and is listed under known limitations in the README; `kill -CONT` recovers.

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
  pid is a new entry. (The original aborted on `assert()`.)
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

## 7. One limiter per target (`runtime/lock.c`)

Two limiters on one target fight each other (each one's `SIGCONT` undoes the
other's `SIGSTOP`). A limiter takes an exclusive `fcntl` record lock on
`<rundir>/<pid>-<starttime>.lock`. The kernel drops the lock when the holder
dies, so there are no stale locks. `rundir` is `$XDG_RUNTIME_DIR/cpulimit` or
`/tmp/cpulimit-<uid>`, created `0700` and verified (owner, mode, not a symlink)
before use. Keying on the start time means a recycled pid is a different target.

## 8. Verification

| Property | Technique | Where |
|---|---|---|
| Parsers accept exactly the grammar | unit + property tests; libFuzzer and a standalone mutation driver, both with checked properties (not only "no crash") | `test/*_test.c`, `fuzz/` |
| Controller invariants L1-L4, tick conversion, `/proc` parser memory safety | harnesses run as random tests on every push (`make verif-sim`) and as bounded proofs under CBMC (`make verif-cbmc`, workflow `cbmc.yml`, experimental) | `verif/` |
| Memory safety / leaks / UB | ASan, UBSan, clang integer sanitizers, valgrind, allocation-failure injection | CI `verify` |
| `list.c` | model-based test against an array, allocation failure | `test/unit/list_test.c` |
| `snapshot.c` | fixed-point model on random process tables, cycles, allocation failure | `test/unit/snapshot_test.c` |
| Process table behavior | scripted iterator (deterministic) | `test/unit/group_test.c` |
| Limiting accuracy, signal handling, S1 | integration tests with real processes | `test/integration/integration_test.sh` |
| Fixed defects stay fixed | 25 mutants, each must fail a unit test | `tools/mutation.sh` |
| Load, leaks over time | fork/exit churn under `-i`, 300 children, RSS flat, valgrind | `test/integration/stress_test.sh` |
| Test adequacy | line and branch coverage by gcov, including daemon children | `tools/coverage.sh` |
| Static analysis | `gcc -fanalyzer` (must be silent), clang-tidy, cppcheck, scan-build, CodeQL | `make analyze`, CI |
| Performance | accuracy, limiter CPU, syscalls/s, `-i` scaling against a baseline binary | `tools/bench.sh` |
| Portability | CI matrix: OS x compiler x libc x architecture | `.github/workflows/` |

Limits of this approach:

* CBMC proofs are bounded (loops unwound to a stated depth) and cover three pure functions only.
  Everything else is tested, not proved. The harnesses also run in simulation mode and against
  mutants. Which harnesses CBMC has finished is in [VERIFICATION.md](VERIFICATION.md).
* The macOS, FreeBSD, OpenBSD, NetBSD and Solaris iterators were written or adapted without a host to
  compile them on. CI builds macOS, FreeBSD and OpenBSD (Solaris is not built), and those jobs do
  not gate until they have passed once.
* Unreachable defensive clamps in `core/limiter.c` (lines that no input can reach) are not covered by
  tests; the CBMC harness is what shows they are unreachable.
* `SIGKILL` or an OOM kill of cpulimit during a stop slice leaves the target stopped, and the
  pid can be recycled between the table update and the signal (one cycle). Neither can be fixed
  in user space without a supervising process, which would reintroduce the orphaned-limiter defect.
