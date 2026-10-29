# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Shared by the integration tests (sourced, not run): paths of the binaries, a private work and
# lock directory, the ok/bad counters and the helpers that read /proc. Sourcing it on a system
# without Linux's /proc ends the calling test with "skip".
# The caller prints its summary with:  summary
BUILD=${BUILD:-build}
BIN=$BUILD/cpulimit
BUSY=$BUILD/test/busy
SPAWN=$BUILD/test/spawn

if [ ! -d /proc/self ] || [ "$(uname -s)" != "Linux" ]; then
    echo "skip  integration tests need Linux /proc"
    exit 0
fi
for f in "$BIN" "$BUSY" "$SPAWN"; do
    [ -x "$f" ] || { echo "FAIL  missing $f (run make)"; exit 1; }
done
# absolute paths: some tests change directory
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
BUSY=$(cd "$(dirname "$BUSY")" && pwd)/$(basename "$BUSY")
SPAWN=$(cd "$(dirname "$SPAWN")" && pwd)/$(basename "$SPAWN")

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cpulimit-it.XXXXXX") || exit 1
export XDG_RUNTIME_DIR=$WORK/run          # locks are private to this run
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

failed=0
passed=0
PIDS=""

ok()   { passed=$((passed + 1)); printf 'ok    %s\n' "$1"; }
bad()  { failed=$((failed + 1)); printf 'FAIL  %s\n' "$1"; [ -n "$2" ] && printf '        %s\n' "$2"; }
check() { # check "description" command...
    desc=$1
    shift
    if "$@"; then ok "$desc"; else bad "$desc"; fi
}

cleanup() {
    for p in $PIDS; do kill -9 "$p" 2>/dev/null; done
    # anything still running from the work directory (copies of the binaries made by the tests)
    for p in $(pgrep -f "$WORK" 2>/dev/null); do
        [ "$p" != "$$" ] && kill -9 "$p" 2>/dev/null
    done
    rm -rf "$WORK"
}
# a signal ends the run (a plain handler would let the script carry on in a deleted directory)
trap cleanup EXIT
trap 'exit 130' INT TERM

track() { PIDS="$PIDS $1"; }

# state letter of a process (R, S, T, Z...), empty if it does not exist
state() { sed -n 's/^.*) \(.\) .*/\1/p' "/proc/$1/stat" 2>/dev/null; }
alive() { kill -0 "$1" 2>/dev/null; }
# CPU ticks (user+system) used so far by a process, with all its threads
ticks() { sed 's/^.*) //' "/proc/$1/stat" 2>/dev/null | awk '{print $12 + $13}'; }
children_of() {
    for d in /proc/[0-9]*; do
        p=${d#/proc/}
        pp=$(sed -n 's/^.*) . \([0-9][0-9]*\) .*/\1/p' "$d/stat" 2>/dev/null)
        [ "$pp" = "$1" ] && echo "$p"
    done
}

# wait_for SECONDS command...: run the command every 50 ms until it succeeds
wait_for() {
    n=$(($1 * 20))
    shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.05
        n=$((n - 1))
    done
    return 1
}
gone() { ! alive "$1"; }
is_stopped() { [ "$(state "$1")" = "T" ]; }
not_stopped() { [ "$(state "$1")" != "T" ]; }

# Has the target been stopped at least once within SECONDS? (it spends most of its time stopped at a low limit)
saw_stopped() {
    n=$(($2 * 40))
    while [ "$n" -gt 0 ]; do
        is_stopped "$1" && return 0
        sleep 0.025
        n=$((n - 1))
    done
    return 1
}

start_busy() {
    "$SPAWN" "$BUSY" > /dev/null 2>&1 &
    BP=$!
    track "$BP"
}


summary() {
    printf '\n%d passed, %d failed\n' "$passed" "$failed"
    [ "$failed" -eq 0 ]
}
