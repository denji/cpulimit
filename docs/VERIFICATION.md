# Verification and benchmarks


Everything below is in the repository. CI runs it on every push (current results: the
[Actions page](https://github.com/denji/cpulimit/actions)). "Local" means a run on a Linux x86_64
development host; the test, mutation and coverage numbers are from the tree after 1.0.3, the benchmark is from 1.0.0.

| Technique | What it checks | Result |
|---|---|---|
| `make check` | 13 suites: iterator, utils, controller, parser, process table, snapshot selection, list, CLI, lock, CLI end-to-end, 55 integration checks with real processes, 123 checks of cpulimit together with nice, ionice, taskset, chrt, env, nohup, timeout and setsid | passing: gcc and clang `-O2 -Werror`, ASan+UBSan, clang integer sanitizers |
| Fuzzing (`make fuzz-standalone`, `make fuzz-libfuzzer`) | `/proc/<pid>/stat` parser and CLI parser with checked properties | standalone driver: 300k+ mutations per target clean locally; libFuzzer jobs pass in CI. The harness found one real bug (below) |
| `make verif-sim`, `make verif-cbmc` | invariants of the controller, exactness of tick conversion, parser memory safety | simulation passes on every push and found one real bug. CBMC runs in its own workflow (`cbmc.yml`, manual and weekly) and is experimental: it found one real defect (fixed in 1.0.1) but has not yet produced a verdict for the other harnesses, so nothing is claimed proved (see below) |
| `tools/mutation.sh` | 25 re-introduced defects | 25/25 detected |
| `tools/coverage.sh` | gcov line/branch coverage, daemon children included | 90.1% lines, 80.9% branches (gate: 85% lines) |
| `make stress` | 15 s fork/exit churn under `-i`, 300 children, no leaks, nothing left stopped, valgrind clean | passing |
| `make analyze` | `gcc -fanalyzer`, clang-tidy | clean |
| cppcheck, CodeQL | | pass in CI |

Bugs found by this process rather than by inspection: `ticks_to_ms()` returned a negative number
for `ticks = 2^63 + 1` at 1000 Hz and saturated wrongly for clock rates above 1.8e16 (neither
reachable with a real `CLK_TCK`), and the original parsed `-l` with `atoi`.

## Not verified

* macOS, FreeBSD and Solaris on real hardware. macOS and OpenBSD are built and unit-tested in CI, FreeBSD is only built, Solaris has never been compiled (the `vm` workflow is the first attempt). Real syslog delivery.
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
  below 2^48, clock rate up to 2^20; inputs up to 12 bytes) and have not been re-run. The CBMC
  installed in CI offers no CaDiCaL option; Bitwuzla through SMT2 failed with a verification error.
* No ACSL/Frama-C specification exists; AFL++ was not run.

## Benchmarks

`make bench BASELINE=/path/to/old/cpulimit` compares two binaries back to back (`tools/bench.sh`).
One run on a 2-CPU virtual machine, 8 s per case, baseline = upstream 0.3.2:

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
