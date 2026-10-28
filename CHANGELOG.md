# Changelog

All notable changes to this fork of cpulimit. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/). The command-line interface and the exit statuses
documented in `man cpulimit` are the public interface that 1.x keeps stable.

## [Unreleased]

## [1.0.3] - 2026-10-28

New platforms (OpenBSD, NetBSD), one shared way for the iterators to find a process and its
descendants, a FreeBSD iterator that no longer misses grandchildren, a formatter and a wider CI
matrix. Options, exit statuses and log text are unchanged; the throttling logic is the 1.0.2 logic.

Upgrading from 1.0.2: nothing to do. On FreeBSD, `-i` now limits the whole process tree, as on
Linux (see Fixed).

### Added
- OpenBSD support: a process iterator over `sysctl` (`src/platform/openbsd/iterator.c`, no libkvm)
  and an OpenBSD CI job (8.0 and 7.9). CPU time is read from the statistics tick counters,
  because `p_rtime` only advances when a process is switched out (200 ms steps for a busy
  process), and zombies are recognised by `p_stat == SDEAD`. The unit and command-mode tests pass
  on 7.9; 8.0 passed all but a per-sample timing bound of the unit test, which is now looser on
  OpenBSD (statistics ticks are taken at randomised instants).
- NetBSD support: `src/platform/netbsd/iterator.c`. OpenBSD and NetBSD share
  `src/platform/bsd_sysctl.h` over `KERN_PROC` / `KERN_PROC2`. Written from the NetBSD sources;
  not yet run on NetBSD.
- `vm` workflow (manual and weekly, report only) on QEMU VMs from `vmactions`: FreeBSD 15.1 and
  14.5, HardenedBSD, GhostBSD, NetBSD 11.0 and 10.1, Solaris, OmniOS and OpenIndiana (build and
  unit tests); Debian 13 on riscv64, ppc64le and aarch64 (full suite including the integration
  tests); probes for GNU/Hurd and Haiku. Compiler package names for some systems are unconfirmed.
- Each VM job copies its transcript back and prints it as a GitHub annotation
  (`tools/vm_build.sh`, `tools/vm_probe.sh`, `tools/vm_notice.sh`), because the VM actions only
  report "ssh exited with code N". The CodeQL job prints its findings the same way
  (`tools/sarif_annotations.py`), since the code scanning page needs a login.
- `AUTHORS`, `docs/UPGRADING.md` (differences from the original cpulimit that scripts can notice)
  and `docs/VERIFICATION.md` (what is tested, what is not, benchmarks).

### Fixed
- FreeBSD: `-i` limited only the direct children of the target, not grandchildren. The iterator now
  selects the whole tree, as the Linux iterator always did. It also bounds the copy of argv[0] into
  the command buffer itself instead of relying on libkvm's limit, skips zombies and kernel
  processes (zombies were listed), reads argv only when it is needed, and closes the kvm descriptor
  on every error path. Run only through the build and the unit tests of the `vm` workflow so far.

### Changed
- One implementation of "the target and all its descendants" (`src/platform/snapshot.c`, O(n log n),
  safe against a pid that is its own parent) for the Linux, macOS, FreeBSD, OpenBSD and NetBSD
  iterators; it replaces a copy per platform, three of them quadratic or worse. The Linux behaviour
  is unchanged. Unit tests compare it with a plain fixed-point model on 20000 random process
  tables and inject every allocation failure (`test/unit/snapshot_test.c`); five mutants of it are
  killed by the mutation script (25 of 25 now). `make coverage` and the valgrind job listed the unit
  tests by hand and would have missed the new one; both now include it.
- Formatting: `.clang-format` (4 spaces, attached braces, `char *p`, 120 columns) replaces
  `.astylerc`. `.format.sh`, `make format`, `make format-check` and a `format` job in
  `verify.yml` (clang-format 18). The tree was reformatted; spacing and wrapping only.
- Copyright: `Copyright (C) 2016-2017, 2019, 2026 Denys Denysov` and
  `SPDX-License-Identifier: GPL-2.0-or-later` in the files written for this fork; e-mail addresses
  removed from the source headers; a COPYRIGHT section in the man page.
- README reduced to install, usage, platforms and known limitations; the rest moved to `docs/`.
- Source file headers say "limit the CPU usage of a process" (as the man page does) instead of "a CPU
  limiter for Linux", which stopped being true with the BSD, macOS and Solaris iterators.
- `tools/ci_run.sh` is POSIX sh (no bash, no `pipefail`), so it also runs inside the BSD VMs.
- The platform table states what CI really covers: macOS builds and runs the unit tests, FreeBSD
  only builds, Solaris has never been compiled.

## [1.0.2] - 2026-10-24

Maintenance release: source tree reorganized, CI repaired. No change to options, exit statuses or
log text; the throttling logic is the 1.0.1 logic.

### Fixed
- `bench` workflow: the baseline build assumed the original `src/Makefile` layout and failed for any
  modernized ref. `tools/build_ref.sh REF DIR` builds either layout and prints the binary path.
- CBMC harnesses: the controller harness excludes an infinite measurement under CBMC (CBMC's NaN
  check flags every division by infinity; finite / infinity is 0), the tick-conversion and
  short-input parser harnesses use smaller bounded domains (the full-range formulas never finished
  building). `verify_parse_stat_line` has been verified once by CBMC (run 39890593464).

### Tests
- `compute_slices`: a NaN limit on the first cycle and an infinite measurement are now tested. This
  kills the mutant "NaN not mapped to 1", which survived once the mutation script ran against a
  verified baseline (20 of 20 killed).

### Changed
- Source tree reorganized by role, no behavior change (same binary logic, same tests, coverage
  89.2% / 78.6% as before): `src/{cli,core,runtime,platform,util}/`, one directory per platform
  under `src/platform/`, tests split into `test/{unit,integration,support}/`, `scripts/` and
  `bench/` merged into `tools/`. `src/cpulimit` is still built for scripts that expect it.
- CBMC moved out of the push workflows into `cbmc.yml` (manual and weekly), one job per harness
  and solver: the default solver, and CaDiCaL when the installed CBMC offers it (Bitwuzla through
  SMT2 was tried and failed). Exit status and time go to the job summary; the newest CBMC release
  is installed by `tools/install_cbmc.sh`. The random-test harnesses (`make verif-sim`) became a
  required `verify` job.
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
- **Not verified:** macOS 14/15 and FreeBSD 14.1 pass in CI but cpulimit has not been run
  on them by hand; Solaris/illumos has never been built.
- **CBMC:** the bounded-model-checking jobs fail and are experimental; no property is claimed
  proved. The same properties are checked by randomized simulation (`make verif-sim`).
- The release workflow (static musl binaries for seven architectures) had not been run when this
  entry was written.

[Unreleased]: https://github.com/denji/cpulimit/compare/1.0.3...HEAD
[1.0.3]: https://github.com/denji/cpulimit/releases/tag/1.0.3
[1.0.2]: https://github.com/denji/cpulimit/releases/tag/1.0.2
[1.0.1]: https://github.com/denji/cpulimit/releases/tag/1.0.1
[1.0.0]: https://github.com/denji/cpulimit/releases/tag/1.0.0
