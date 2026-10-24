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
are attached to tagged releases (see [CHANGELOG.md](CHANGELOG.md) for the release status).

| Platform | Status |
|---|---|
| Linux, glibc | CI: x86_64 and arm64, gcc and clang, `-O0`/`-O2`/`-O3`, `-std=gnu99` to `gnu2x` |
| Linux, musl | CI: x86_64, i386, arm64, armv7, ppc64le, s390x, riscv64 (qemu-user except x86_64) |
| macOS 14/15 | CI builds and runs the unit tests (not the integration tests); the job does not gate a release |
| FreeBSD 14.1 | CI builds only; the job does not gate a release |
| Solaris, illumos | iterator contributed in [#3](https://github.com/denji/cpulimit/pull/3); never compiled so far; the experimental `vm` workflow tries Solaris, OmniOS and OpenIndiana (report only) |
| OpenBSD, NetBSD, DragonFly | not supported (no process iterator) |

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

## Documentation

* [`man cpulimit`](man/cpulimit.1): options, exit statuses, signals, lock files.
* [docs/UPGRADING.md](docs/UPGRADING.md): differences from the original cpulimit that scripts can notice.
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): modules, invariants, the tests that enforce them.
* [docs/VERIFICATION.md](docs/VERIFICATION.md): what is tested, what is not, benchmarks.
* [CHANGELOG.md](CHANGELOG.md), [project page](https://denji.github.io/cpulimit/).

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

## License

Copyright (C) 2005-2012 Angelo Marletta<br>
Copyright (C) 2016-2017, 2019, 2026 Denys Denysov

GPL-2.0-or-later, see [LICENSE](LICENSE). Contributors are listed in [AUTHORS](AUTHORS).
