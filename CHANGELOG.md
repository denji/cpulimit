# Changelog

All notable changes to this fork of cpulimit. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The command-line interface and the exit statuses
documented in `man cpulimit` are the public interface that 1.x keeps stable.

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

[1.0.0]: https://github.com/denji/cpulimit/releases/tag/1.0.0
