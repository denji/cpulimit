# Changelog

All notable changes to this fork of cpulimit. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The command-line interface and the exit statuses
documented in `man cpulimit` are the public interface that 1.x keeps stable.

## [Unreleased]

### Fixed
- `bench` workflow: the baseline build assumed the original `src/Makefile` layout and failed for any
  modernized ref. `tools/build_ref.sh REF DIR` builds either layout and prints the binary path.
- CBMC harnesses: the controller harness excludes an infinite measurement under CBMC (CBMC's NaN
  check flags every division by infinity; finite / infinity is 0), the tick-conversion and
  short-input parser harnesses use smaller bounded domains (the full-range formulas never finished
  building). `verify_parse_stat_line` has been verified once by CBMC (run 39890593464).

### Changed
- Source tree reorganized by role, no behavior change (same binary logic, same tests, coverage
  89.2% / 78.6% as before): `src/{cli,core,runtime,platform,util}/`, one directory per platform
  under `src/platform/`, tests split into `test/{unit,integration,support}/`, `scripts/` and
  `bench/` merged into `tools/`. `src/cpulimit` is still built for scripts that expect it.
- `compute_slices`: the rate is `target / pcpu` (or 1), which cannot produce a NaN from an infinite
  measurement; new limiter tests and the mutation suite (20/20) cover it.
- CBMC moved out of the push workflows into `cbmc.yml` (manual and weekly): one job per harness and
  solver back end (default, CaDiCaL when the installed CBMC offers it; Bitwuzla via SMT2 was tried and failed), exit status and
  time in the job summary, newest CBMC release installed by `tools/install_cbmc.sh`. The random-test
  harnesses (`make verif-sim`) became a required `verify` job.
- `cputime` units and per-platform resolution documented in `src/platform/iterator.h`. A nanosecond
  CPU clock for the Linux single-target path was tried and dropped: no measurable accuracy gain in
  `make bench`, two more system calls per cycle.

## [1.0.1] - 2026-10-22

Maintenance release: one internal fix found by the bounded model checker, and verification work.
No change to options, exit statuses or log text.

### Fixed
- `compute_slices()` computed `0 / NaN` when the measured CPU usage was a NaN and the previous
  duty cycle was zero, then mapped the NaN to "run fully" afterwards. It now takes the division
  only when it is a real quotient below 1: `(target < pcpu) ? target / pcpu : 1`. The result is the
  same for every input, and no NaN or infinity is produced. (A measured usage cannot be a NaN in
  normal operation; this is a defect of the arithmetic, not of observed behaviour.)

### Changed
- Verification harnesses (`verif/`): the CBMC jobs now report how CBMC ended and stop at the
  first violated property with a trace; `verify_ticks` and `verify_parse_stat` use smaller bounded
  domains (clock rates below 1.8e16 and parser inputs up to 24 bytes under CBMC; the full range stays
  in the randomized simulation and the unit tests); new `verify_parse_stat_line` asserts that state,
  ppid, utime, stime and starttime come from exactly fields 3, 4, 14, 15 and 22 of a well-formed
  line. `CBMC_SOLVER=auto` selects CaDiCaL when the installed CBMC offers it.
- Release workflow: `1.0.1` and `v1.0.1` style tags both trigger it, the tag must equal the version
  built, and the release body is taken from this file.
- The CBMC jobs had not completed successfully when this entry was written; nothing is claimed
  proved.

## [1.0.0] - 2026-10-21

First release of the fork after the redesign. Starts from the upstream 0.3.2 code base.

### Added
- Daemon mode (`-d`), syslog output (`-s`), pid file (`-P`), long options
  ([issue #1](https://github.com/denji/cpulimit/issues/1)).
- One limiter per target: a second cpulimit on the same process is refused (exit 1) and names the
  first; the lock is an `fcntl` record lock and vanishes with its holder, even on `SIGKILL`.
  `--no-lock` overrides it.
- Command mode runs the command and the control loop in a single process; the exit status is the
  command's.
- `--no-boost`, `-V/--version`.
- Process identity by (pid, start time), so a recycled pid is not limited by mistake.
- `make install/uninstall/static/check/analyze/stress/bench/coverage/fuzz-*/verif-*`, a man page,
  `docs/ARCHITECTURE.md`.
- Tests: 10 suites including 55 integration checks with real processes, fuzz targets,
  a model-based list test, failure-injection allocator, 20-mutant mutation script, stress and
  benchmark scripts.
- GitHub Actions: Linux matrix (glibc and musl on seven architectures, gcc and clang, several C
  standards), sanitizers, valgrind, static analysis, CodeQL, fuzzing, coverage gate, benchmarks,
  release workflow. Replaces Travis CI.

### Changed
- Strict option parsing: `-l 5x` and `-l abc` are rejected (they were accepted); `-m` no longer
  crashes; a zero or oversized `-c` no longer spins or fails in `nanosleep`.
- The Linux `/proc` reader is anchored on the last `)`, uses strict 64-bit parsing, skips
  zombies and reads the command only when needed; the `-i` scan builds one sorted parent table.
- Signals: a flag-only handler, blocked except inside `pselect()`; `CLOCK_MONOTONIC` instead of the
  wall clock; every allocation is checked.
- Measured on one 2-CPU VM: syscalls per second 212 to 88 (one target), limiter CPU 5.0 % to
  2.4 % with 200 children under `-i`. Control accuracy is **not** improved (same controller).
- Build: one non-recursive Makefile replaces the recursive ones.

### Fixed
- `ticks_to_ms()` returned a negative value for `ticks = 2^63 + 1` at 1000 Hz and saturated wrongly
  for clock rates above 1.8e16 (found by the verification harnesses; neither reachable with a real
  `CLK_TCK`).
- Duplicated output, invalid `nanosleep` slices, `-m` crash.

### Known limitations
- If cpulimit is killed with `SIGKILL` (or by the OOM killer) during a stop slice, the target stays
  stopped; send it `SIGCONT`.
- A pid can be recycled between reading a process and signalling it; one control cycle of window
  remains.
- Multi-threaded targets are limited as a whole, not per thread.
- **Not verified:** macOS 14/15 and FreeBSD 14.1 pass in CI but the author has never run cpulimit
  on them by hand; Solaris/illumos has never been built.
- **CBMC:** the bounded-model-checking jobs fail and are experimental; no property is claimed
  proved. The same properties are checked by randomized simulation (`make verif-sim`).
- The release workflow (static musl binaries for seven architectures) had not been run when this
  entry was written.

[1.0.1]: https://github.com/denji/cpulimit/releases/tag/1.0.1
[1.0.0]: https://github.com/denji/cpulimit/releases/tag/1.0.0
