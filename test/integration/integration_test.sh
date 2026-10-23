#!/bin/sh
# Integration tests: the real cpulimit against real processes. Linux only (reads /proc).
# Usage: BUILD=build sh test/integration/integration_test.sh
#   CPULIMIT_SKIP_TIMING=1   skip the checks that measure CPU shares (emulators, overloaded hosts)

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

# ---------------------------------------------------------------- signals, attach mode
for sig in INT TERM HUP QUIT; do
    start_busy
    "$SPAWN" "$BIN" -l 10 -p "$BP" > "$WORK/out.$sig" 2>&1 &
    LP=$!
    track "$LP"
    if saw_stopped "$BP" 5; then ok "SIG$sig: the limiter starts stopping the target"; else bad "SIG$sig: the limiter never stopped the target"; fi
    kill -"$sig" "$LP"
    if wait_for 3 gone "$LP"; then ok "SIG$sig: the limiter exits"; else bad "SIG$sig: the limiter did not exit"; kill -9 "$LP"; fi
    wait "$LP" 2>/dev/null
    rc=$?
    check "SIG$sig: exit status 0" [ "$rc" -eq 0 ]
    # invariant S1: nothing is left stopped by the limiter that just left
    sleep 0.1
    if not_stopped "$BP"; then ok "SIG$sig: target is not left stopped"; else bad "SIG$sig: target left stopped (state $(state "$BP"))"; fi
    kill -9 "$BP"; wait "$BP" 2>/dev/null
done

# ---------------------------------------------------------------- command mode: no orphan, no extra process
"$SPAWN" "$BIN" -l 10 "$BUSY" > "$WORK/cmd.out" 2>&1 &
LP=$!
track "$LP"
wait_for 3 sh -c "[ -n \"\$(cat /proc/$LP/task/$LP/children 2>/dev/null)\" ] || [ -n \"\$(for d in /proc/[0-9]*; do sed -n 's/^.*) . \\([0-9][0-9]*\\) .*/\\1/p' \$d/stat 2>/dev/null; done | grep -x $LP)\" ]"
sleep 0.5
KIDS=$(children_of "$LP")
NKIDS=$(printf '%s\n' "$KIDS" | grep -c .)
check "command mode: cpulimit has exactly one child (the target, no separate limiter)" [ "$NKIDS" -eq 1 ]
TARGET=$(printf '%s\n' "$KIDS" | head -1)
track "$TARGET"
saw_stopped "$TARGET" 5 && ok "command mode: the target is being limited" || bad "command mode: the target was never stopped"
kill -TERM "$LP"
wait_for 3 gone "$LP" && ok "command mode: SIGTERM to the limiter ends it" || bad "command mode: limiter survived SIGTERM"
wait "$LP" 2>/dev/null
sleep 0.1
# this is the defect the redesign removes: signalling only the parent used to leave a limiter running
LEFT=$(for d in /proc/[0-9]*; do
    p=${d#/proc/}
    [ "$p" = "$TARGET" ] && continue
    if grep -q "$BIN" "$d/cmdline" 2>/dev/null && [ "$(sed -n 's/^.*) . \([0-9][0-9]*\) .*/\1/p' "$d/stat" 2>/dev/null)" != "" ]; then echo "$p"; fi
done | grep -v "^$$\$" | wc -l)
check "command mode: no limiter process is left behind" [ "$LEFT" -eq 0 ]
if not_stopped "$TARGET"; then ok "command mode: target is not left stopped"; else bad "command mode: target left stopped"; fi
kill -9 "$TARGET" 2>/dev/null

# ---------------------------------------------------------------- exit statuses (command mode)
"$BIN" -l 100 sh -c 'exit 7' > /dev/null 2>&1
check "command exit status 7 is returned" [ $? -eq 7 ]
OUT=$("$BIN" -l 100 sh -c 'kill -9 $$' 2>&1)
RC=$?
check "a command killed by a signal gives the raw wait status (9)" [ "$RC" -eq 9 ]
case $OUT in *"terminated abnormally"*) ok "killed command: message printed" ;; *) bad "killed command: no 'terminated abnormally' message" "$OUT" ;; esac
"$BIN" -l 100 /nonexistent/program > "$WORK/nx.out" 2>&1
check "a command that cannot be executed fails" [ $? -ne 0 ]

# ---------------------------------------------------------------- an ignored signal stays ignored (nohup)
start_busy
"$SPAWN" -i HUP "$BIN" -l 10 -p "$BP" > /dev/null 2>&1 &
LP=$!
track "$LP"
sleep 0.5
kill -HUP "$LP"
sleep 0.5
check "SIGHUP ignored at startup (nohup) keeps the limiter running" alive "$LP"
kill -TERM "$LP"
wait_for 3 gone "$LP" && ok "...and SIGTERM still stops it" || bad "...SIGTERM did not stop it"
kill -9 "$BP"; wait "$BP" 2>/dev/null; wait "$LP" 2>/dev/null

# ---------------------------------------------------------------- SIGPIPE must not kill the limiter
start_busy
# stdout is a pipe whose reader goes away right after the limiter has started (like "cpulimit ... | head")
mkfifo "$WORK/fifo"
( : < "$WORK/fifo" ) &
"$SPAWN" "$BIN" -v -l 20 -p "$BP" > "$WORK/fifo" 2>&1 &
VP=$!
track "$VP"
sleep 2.5                                   # long enough for several lines to be written to the closed pipe
check "closed stdout (SIGPIPE) does not kill the limiter" alive "$VP"
kill -TERM "$VP" 2>/dev/null
wait_for 3 gone "$VP"
wait "$VP" 2>/dev/null
sleep 0.1
if not_stopped "$BP"; then ok "...and the target is not left stopped"; else bad "...target left stopped"; fi
kill -9 "$BP"; wait "$BP" 2>/dev/null

# ---------------------------------------------------------------- one limiter per target
start_busy
"$SPAWN" "$BIN" -l 20 -p "$BP" > /dev/null 2>&1 &
L1=$!
track "$L1"
sleep 0.7
OUT=$("$BIN" -l 20 -p "$BP" 2>&1)
RC=$?
check "a second limiter on the same target is refused (exit 1)" [ "$RC" -eq 1 ]
case $OUT in *"already being limited"*) ok "...with an explanation" ;; *) bad "...no explanation" "$OUT" ;; esac
"$SPAWN" "$BIN" --no-lock -l 20 -p "$BP" > /dev/null 2>&1 &
L2=$!
track "$L2"
sleep 0.7
check "--no-lock lets a second limiter run" alive "$L2"
kill -TERM "$L2"; wait_for 3 gone "$L2"; wait "$L2" 2>/dev/null
kill -TERM "$L1"; wait_for 3 gone "$L1"; wait "$L1" 2>/dev/null
"$SPAWN" "$BIN" -l 20 -p "$BP" > /dev/null 2>&1 &
L3=$!
track "$L3"
sleep 0.7
check "after the first limiter left, a new one is accepted (no stale lock)" alive "$L3"
kill -9 "$L3"; wait "$L3" 2>/dev/null          # crash: no cleanup runs
kill -CONT "$BP"
"$SPAWN" "$BIN" -l 20 -p "$BP" > /dev/null 2>&1 &
L4=$!
track "$L4"
sleep 0.7
check "after a limiter was killed with SIGKILL, a new one is accepted" alive "$L4"
kill -TERM "$L4"; wait_for 3 gone "$L4"; wait "$L4" 2>/dev/null
kill -9 "$BP"; wait "$BP" 2>/dev/null

# ---------------------------------------------------------------- by name (-e), and never matching itself
cp "$BUSY" "$WORK/cl_target_$$"
"$SPAWN" "$WORK/cl_target_$$" > /dev/null 2>&1 &
BP=$!
track "$BP"
"$SPAWN" "$BIN" -l 10 -e "cl_target_$$" > /dev/null 2>&1 &
LP=$!
track "$LP"
# under qemu-user emulation /proc/PID/cmdline starts with the emulator: nothing can match by name
first_word=$(tr '\0' '\n' < "/proc/$BP/cmdline" 2>/dev/null | head -n 1)
if [ -r "/proc/$BP/cmdline" ] && [ "$first_word" != "$WORK/cl_target_$$" ]; then
    printf 'skip  -e by name (cmdline starts with "%s": emulated process)\n' "$first_word"
else
    saw_stopped "$BP" 6 && ok "-e: the process is found by name and limited" || bad "-e: the process was not limited"
fi
kill -TERM "$LP"; wait_for 3 gone "$LP"; wait "$LP" 2>/dev/null
kill -9 "$BP"; wait "$BP" 2>/dev/null

cp "$BIN" "$WORK/cl_self_$$"
OUT=$("$WORK/cl_self_$$" -l 10 -z -e "cl_self_" 2>&1)
RC=$?
case $OUT in
*"No process found"*) ok "-e never matches cpulimit itself" ;;
*) bad "-e matched itself or failed (rc $RC)" "$OUT" ;;
esac

# ---------------------------------------------------------------- daemon
start_busy
PF=$WORK/d.pid
timeout_run() { # timeout_run SECONDS command...: kill the command if it takes longer
    t=$1
    shift
    "$@" &
    cp=$!
    n=$((t * 20))
    while [ "$n" -gt 0 ] && alive "$cp"; do sleep 0.05; n=$((n - 1)); done
    if alive "$cp"; then kill -9 "$cp"; wait "$cp" 2>/dev/null; return 124; fi
    wait "$cp"
}
timeout_run 5 "$SPAWN" "$BIN" -d -P "$PF" -l 20 -p "$BP" > "$WORK/d.out" 2>&1
check "daemon: the command returns 0 once the daemon is up" [ $? -eq 0 ]
DP=$(cat "$PF" 2>/dev/null)
check "daemon: the pid file holds a live pid" sh -c "[ -n '$DP' ] && kill -0 '$DP'"
track "$DP"
check "daemon: it has no controlling terminal" [ "$(sed 's/^.*) //' /proc/$DP/stat | awk '{print $5}')" = "0" ]
check "daemon: stdin/stdout/stderr are /dev/null" sh -c "[ \"\$(readlink /proc/$DP/fd/0)\" = /dev/null ] && [ \"\$(readlink /proc/$DP/fd/1)\" = /dev/null ] && [ \"\$(readlink /proc/$DP/fd/2)\" = /dev/null ]"
check "daemon: working directory is /" [ "$(readlink "/proc/$DP/cwd")" = "/" ]
check "daemon: it is not a child of the test shell" [ "$(sed 's/^.*) . \([0-9]*\) .*/\1/p' "/proc/$DP/stat" | head -1)" != "$$" ]
saw_stopped "$BP" 5 && ok "daemon: it limits the target" || bad "daemon: the target was never stopped"
OUT=$("$BIN" -d -P "$PF" -l 20 -p "$BP" 2>&1)
RC=$?
check "daemon: a second daemon with a live pid file is refused" [ "$RC" -eq 1 ]
case $OUT in *"File exists"*) ok "...with the reason" ;; *) bad "...no reason given" "$OUT" ;; esac
kill -TERM "$DP"
wait_for 3 gone "$DP" && ok "daemon: SIGTERM stops it" || bad "daemon: SIGTERM did not stop it"
sleep 0.1
check "daemon: it removes its pid file on exit" [ ! -e "$PF" ]
if not_stopped "$BP"; then ok "daemon: the target is not left stopped"; else bad "daemon: target left stopped"; fi
printf '999999\n' > "$PF"
timeout_run 5 "$SPAWN" "$BIN" -d -P "$PF" -l 20 -p "$BP" > /dev/null 2>&1
check "daemon: a stale pid file (dead pid) is replaced" [ $? -eq 0 ]
DP=$(cat "$PF"); track "$DP"
check "daemon: ...by the new pid" [ "$DP" != "999999" ]
kill -TERM "$DP"; wait_for 3 gone "$DP"
(cd "$WORK" && timeout_run 5 "$SPAWN" "$BIN" -d -P rel.pid -l 20 -p "$BP" > /dev/null 2>&1)
check "daemon: a relative pid file lands where the user ran the command" [ -s "$WORK/rel.pid" ]
DP=$(cat "$WORK/rel.pid" 2>/dev/null); track "$DP"
kill -TERM "$DP"; wait_for 3 gone "$DP"
kill -9 "$BP"; wait "$BP" 2>/dev/null

# ---------------------------------------------------------------- how well the limit holds
measure() { # measure LIMIT SECONDS [busy args] -> prints the share of one CPU used, in percent
    lim=$1; secs=$2; shift 2
    "$SPAWN" "$BUSY" "$@" > /dev/null 2>&1 &
    mp=$!
    track "$mp"
    "$SPAWN" "$BIN" -l "$lim" -p "$mp" > /dev/null 2>&1 &
    ml=$!
    track "$ml"
    sleep 1.5
    t0=$(ticks "$mp"); w0=$(date +%s.%N 2>/dev/null || date +%s)
    sleep "$secs"
    t1=$(ticks "$mp"); w1=$(date +%s.%N 2>/dev/null || date +%s)
    kill -TERM "$ml"; wait_for 3 gone "$ml"; wait "$ml" 2>/dev/null
    kill -9 "$mp"; wait "$mp" 2>/dev/null
    awk -v a="$t0" -v b="$t1" -v x="$w0" -v y="$w1" 'BEGIN { printf "%.0f", (b - a) / 100 / (y - x) * 100 }'
}
if [ -n "$CPULIMIT_SKIP_TIMING" ]; then
    echo "skip  limit accuracy (CPULIMIT_SKIP_TIMING set)"
else
    hz=$(getconf CLK_TCK 2>/dev/null || echo 100)
    for lim in 30 70; do
        got=$(measure "$lim" 6)
        lo=$((lim - 12)); hi=$((lim + 12))
        if [ "$hz" = "100" ] && [ "$got" -ge "$lo" ] && [ "$got" -le "$hi" ]; then ok "limit $lim% holds: measured ${got}%"; else bad "limit $lim%: measured ${got}% (accepted $lo-$hi%, clock ticks $hz/s)"; fi
    done
    got=$(measure 50 6 2)      # two threads inside one process: the limit applies to the process as a whole
    if [ "$got" -ge 38 ] && [ "$got" -le 62 ]; then ok "limit 50% holds for a 2-thread process: measured ${got}%"; else bad "limit 50% with 2 threads: measured ${got}%"; fi
    got=$(measure 0 4)
    if [ "$got" -le 8 ]; then ok "limit 0% (almost) stops the target: measured ${got}%"; else bad "limit 0%: measured ${got}%"; fi
fi

printf '\n%d passed, %d failed\n' "$passed" "$failed"
[ "$failed" -eq 0 ]
