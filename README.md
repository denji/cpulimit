# cpulimit

[![ci](https://github.com/denji/cpulimit/actions/workflows/ci.yml/badge.svg)](https://github.com/denji/cpulimit/actions/workflows/ci.yml)
[![verify](https://github.com/denji/cpulimit/actions/workflows/verify.yml/badge.svg)](https://github.com/denji/cpulimit/actions/workflows/verify.yml)
[![fuzz](https://github.com/denji/cpulimit/actions/workflows/fuzz.yml/badge.svg)](https://github.com/denji/cpulimit/actions/workflows/fuzz.yml)

`cpulimit` keeps the CPU use of a process, and optionally its children, near a percentage of one
CPU. It does not touch nice values or scheduling classes: it alternates `SIGSTOP` and `SIGCONT`
within short chunks and corrects the duty cycle from the CPU time the target really used, so it
adapts to system load. Useful for batch jobs that must not eat the machine.

```sh
cpulimit -l 50 -p 1234            # an existing process, half a CPU
cpulimit -l 25 -i -e ffmpeg       # by name, with its children
cpulimit -l 80 -- make -j8        # start a command and limit it
cpulimit -d -s -P /run/cpulimit.pid -l 30 -e backup   # daemon with syslog and pid file
```

## Install

```sh
make && sudo make install          # PREFIX=/usr/local; also DESTDIR=, BINDIR=, MANDIR=
make static STATIC_CC=musl-gcc     # static binary in build/static/
make check                         # unit, CLI and integration tests
```

GNU make is required (`gmake` on the BSDs and illumos). The build is non-recursive, keeps objects in
`build/`, rebuilds when flags change, and never overrides your `CFLAGS`. `make install` does not
call `sudo`. Prebuilt static musl binaries (x86_64, aarch64, armv7, i686, ppc64le, s390x, riscv64)
are attached to tagged releases (the release workflow has not been run yet; see [CHANGELOG.md](CHANGELOG.md)).

| Platform | Status |
|---|---|
| Linux (glibc) | tested on x86_64 by the author; CI passes on x86_64 and arm64, gcc and clang, -O0/-O2/-O3, `-std=gnu99` to `gnu2x` |
| Linux (musl) | CI builds and runs the tests on x86_64, i386, arm64, armv7, ppc64le, s390x and riscv64 (qemu-user for all but x86_64) |
| macOS 14/15, FreeBSD 14.1 | build and unit tests pass in CI; **the author has no such machine**, so nothing was tried by hand. These jobs stay `continue-on-error` |
| Solaris / illumos | **never built**, best effort |

## Usage

```
cpulimit [OPTIONS...] TARGET

  -l, --limit=N                percent of one CPU, 0 to 100 x CPUs (required)
  -c, --chunk-size=N           control chunk in microseconds, 1..1000000 (default 100000)
  -i, --include-children       also limit descendants
  -m, --minimum-limited-cpu=M  processes below M of a CPU (0.05 = 5%) are left running
  -z, --lazy                   exit when there is no target or it dies
  -d, --daemon                 detach and run in the background
  -s, --syslog                 also log to syslog
  -P, --pidfile=FILE           daemon pid file (needs -d); stale files replaced, live ones refused
      --no-lock                allow a target another cpulimit already limits
      --no-boost               do not raise cpulimit's own priority
  -v, --verbose   -V, --version   -h, --help

TARGET is exactly one of:  -p PID | -e NAME_OR_PATH | COMMAND [ARGS]
```

`man cpulimit` has the details: exit statuses, signals, lock files. A few behaviors worth knowing:

* **Command mode is one process.** `cpulimit -l 50 cmd` forks the command and runs the control
  loop itself; the exit status is the command's. Earlier versions left a separate limiter process
  that could outlive its parent.
* **One limiter per target.** A second `cpulimit` on the same process is refused (exit 1) with the
  pid of the first, because two limiters undo each other's stop/continue. The lock is an `fcntl`
  record lock, so it vanishes with its holder even on `SIGKILL`. `--no-lock` overrides it.
* **Signals.** `SIGINT/TERM/HUP/QUIT` release every limited process and exit 0. A signal that was
  already ignored at start (e.g. `SIGHUP` under `nohup`) stays ignored.
* `-h` exits with status 1, as it always has.

## What changed from the original

Behavior that scripts may depend on (log lines, exit statuses, `-p/-e/-l/-c/-i/-m/-v/-z`) is kept.

| Area | Before | Now |
|---|---|---|
| Option parsing | `atoi`: `-l 5x` and `-l abc` accepted; `-m` crashed | strict parsing with ranges; `-c 0` and `-c 1000000+` no longer spin or fail in `nanosleep` |
| Process identity | pid only: a reused pid was limited as if it were the target | pid + start time |
| Waiting | `nanosleep` loop, signals handled in the handler | flag-only handler, signals blocked except in `pselect()` (no lost-wakeup window) |
| Clock | wall clock | `CLOCK_MONOTONIC` |
| Linux `/proc` | tokenised with `atoi` (32-bit, no overflow or error check) | parser anchored on the last `)`, strict 64-bit, zombies skipped, command read only when needed |
| Memory | unchecked `malloc` | checked allocations on every path, failure-injection tested |
| Daemon, syslog, lock, pid file, `--no-boost` | none | added ([issue #1](https://github.com/denji/cpulimit/issues/1)) |
| System calls / limiter CPU | see benchmarks | ~2.4x fewer syscalls per second and ~2x less limiter CPU with 200 children (below); the `-i` scan builds one sorted parent table instead of walking `/proc` per process |
| Build | recursive Makefiles, Travis | one Makefile, GitHub Actions |

The design is described, with the invariants the code is tested against, in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Verification

Everything below is in the repository and is run by CI. "Here" means it was executed on the
author's Linux x86_64 machine while developing this version; the last commit checked in CI
(cf66c4f) passed every job except the three CBMC jobs (see below).

| Technique | What it checks | Result |
|---|---|---|
| `make check` | 10 suites: iterator, utils, controller, parser, process table, list, CLI, lock, CLI end-to-end, 55 integration checks with real processes | passing: gcc and clang `-O2 -Werror`, ASan+UBSan, clang integer sanitizers |
| Fuzzing (`make fuzz-standalone`, `make fuzz-libfuzzer`) | `/proc/<pid>/stat` parser and CLI parser with checked properties | standalone driver: 300k+ mutations per target clean here; libFuzzer jobs pass in CI. The harness found one real bug (below) |
| `make verif-sim`, `make verif-cbmc` | invariants of the controller, exactness of tick conversion, parser memory safety | simulation passes on every push and found one real bug. CBMC runs in its own workflow (`cbmc.yml`, manual and weekly) and is experimental: it found one real defect (fixed in 1.0.1) but has not yet produced a verdict for the other harnesses, so nothing is claimed proved (see below) |
| `scripts/mutation.sh` | 20 re-introduced defects | 20/20 detected after two new tests |
| `scripts/coverage.sh` | gcov line/branch coverage, daemon children included | 89.2% lines, 78.6% branches (gate: 85% lines) |
| `make stress` | 15 s fork/exit churn under `-i`, 300 children, no leaks, nothing left stopped, valgrind clean | passing |
| `make analyze` | `gcc -fanalyzer`, clang-tidy | clean |
| cppcheck, CodeQL | | pass in CI |

Bugs found by this process rather than by inspection: `ticks_to_ms()` returned a negative number
for `ticks = 2^63 + 1` at 1000 Hz and saturated wrongly for clock rates above 1.8e16 (neither
reachable with a real `CLK_TCK`), and the original parsed `-l` with `atoi`.

### Not verified

* macOS, FreeBSD, Solaris on real hardware; Solaris builds at all; real syslog delivery.
* musl on non-x86 runs under qemu-user, which fakes parts of `/proc`; some process-name tests are
  skipped there.
* **CBMC:** four harnesses (controller invariants, tick conversion, `stat` parser rejection paths,
  field positions of a well-formed line), in `cbmc.yml` (manual and weekly, not on pushes).
  **One has a verdict:** `verify_parse_stat_line` (state, ppid, utime, stime and starttime are
  exactly fields 3, 4, 14, 15 and 22 of a 45-byte line with symbolic digits, no overflow or
  out-of-bounds access) was verified by CBMC with its default solver in CI run 39890593464
  (commit 75e6bbe). That is a bounded claim about one line shape, not about the parser. The
  controller harness reported a division by infinity as a NaN source, which is a false positive
  (finite / infinity is 0) and is excluded in the harness; the tick-conversion and short-input
  parser harnesses did not finish building their formulas, so their bounds were narrowed (ticks
  below 2^48, clock rate up to 2^20; inputs up to 12 bytes) and have not been re-run. The installed
  CBMC offers no CaDiCaL option; Bitwuzla through SMT2 failed with a verification error.
* No ACSL/Frama-C specification exists; AFL++ was not run.

## Benchmarks

`make bench BASELINE=/path/to/old/cpulimit` compares two binaries back to back (`bench/bench.sh`).
One run on a 2-CPU virtual machine, 8 s per case, baseline = the code before this work:

| | baseline | now |
|---|---|---|
| CPU share at `-l 10 / 25 / 50 / 75 / 90` | 7.2 / 24.0 / 53.1 / 74.3 / 89.5 % | 7.2 / 23.2 / 52.1 / 74.3 / 89.5 % |
| limiter CPU, one target | 0.25 % | 0.12 % |
| syscalls per second, one target | 212 | 88 |
| limiter CPU, `-i` with 200 children | 5.0 % | 2.4 % |

Accuracy is a wash, not an improvement: both versions use the same controller and both
under-deliver at low limits and overshoot at 50 %. The gains are overhead and the correctness
fixes above. This is a single run on a shared VM with a 10 ms clock tick, so treat differences of
a point or two as noise. An earlier run measured the baseline's single-target limiter CPU at 0.12 %,
so that row is within run-to-run variation; the syscall and `-i` rows were stable across both runs.
The [project page](https://denji.github.io/cpulimit/) plots these numbers.

## Known limitations

* If cpulimit is killed with `SIGKILL` (or by the OOM killer) while the target is in its stop
  slice, the target stays stopped; send it `SIGCONT`. No user-space design avoids this.
* Between reading a process and signalling it, its pid can be recycled. Identity by start time
  closes the long window; a window of one control cycle remains.
* A multi-threaded target is limited as a whole, not per thread. A 2-thread busy loop at `-l 50`
  measured 53-54 % of one CPU over 10 s, with the old and the new binary alike.
* `-m` takes a fraction of a CPU (0.05), unlike `-l`, which is a percentage.

## Contributing

Bug fixes, tests, and ports are welcome. Run `make check analyze verif-sim` before sending a
change, and add a test that fails without it. Changes to the controller or the process table must
keep the invariants in `docs/ARCHITECTURE.md`.

## Credits and license

Originally written by [Angelo Marletta](https://github.com/opsengine). GPL-2.0-or-later, see
`LICENSE`.
