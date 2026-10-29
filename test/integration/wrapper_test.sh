#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# cpulimit together with the usual process wrappers: nice, ionice, taskset, chrt, env, nohup,
# timeout, setsid. Linux only (reads /proc); a wrapper that is missing or not permitted on this
# host is skipped, never failed.
#
# What it pins down:
#   * a target started under a wrapper is limited, and the limiter leaves the target's nice
#     value, I/O class, CPU affinity and scheduling policy exactly as they were;
#   * command mode through a wrapper limits the command and returns its exit status;
#   * -i follows descendants through wrappers that fork (timeout, setsid -w, sh);
#   * cpulimit itself under nice/ionice/taskset still works, --no-boost leaves its priority alone
#     and the default boost reports what it did;
#   * the stop-slice invariant holds when a wrapper ends the limiter (timeout sends SIGTERM).
# Usage: BUILD=build sh test/integration/wrapper_test.sh
#   CPULIMIT_SKIP_TIMING=1   skip the checks that measure CPU shares (emulators, overloaded hosts)

. "$(dirname "$0")/lib.sh"

# does this wrapper work here at all? (a missing binary, a seccomp filter or a missing capability)
works() { "$@" > /dev/null 2>&1; }
skip() { printf 'skip  %s\n' "$1"; }

nice_of() { sed 's/^.*) //' "/proc/$1/stat" 2>/dev/null | awk '{print $17}'; }
ionice_of() { ionice -p "$1" 2>&1; }
affinity_of() { taskset -p "$1" 2>&1; }
policy_of() { chrt -p "$1" 2>&1; }
comm_of() { cat "/proc/$1/comm" 2>/dev/null; }

descendants_of() {
    for c in $(children_of "$1"); do
        echo "$c"
        descendants_of "$c"
    done
}
# the first descendant of $1 that is the busy loop, within SECONDS
busy_below() {
    n=$(($2 * 10))
    while [ "$n" -gt 0 ]; do
        for d in $(descendants_of "$1"); do
            [ "$(comm_of "$d")" = "busy" ] && { echo "$d"; return 0; }
        done
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}

# stop a limiter with SIGTERM and wait for it; sets RC to its exit status
end_limiter() {
    kill -TERM "$1" 2>/dev/null
    wait_for 3 gone "$1" || kill -9 "$1" 2>/dev/null
    wait "$1" 2>/dev/null
    RC=$?
}

# the program a just-started process runs: spawn (and the wrappers) exec, which takes a moment
comm_is() { [ "$(comm_of "$1")" = "$2" ]; }

base_nice=$(nice_of $$)

# Emulated processes (qemu-user) do not show the program's name in comm: nothing below can find
# the busy loop by name there.
start_busy
wait_for 3 comm_is "$BP" busy
BUSY_COMM=$(comm_of "$BP")
kill -9 "$BP"; wait "$BP" 2>/dev/null
if [ "$BUSY_COMM" != "busy" ]; then
    skip "wrapper tests: comm of the busy loop is '$BUSY_COMM' (emulated processes)"
    exit 0
fi

# ---------------------------------------------------------------- attach: the target's attributes survive
# attach_case LABEL WRAPPER... : "spawn WRAPPER... busy" is the target (the wrappers exec, so $! is busy)
attach_case() {
    label=$1; shift
    "$SPAWN" "$@" "$BUSY" > /dev/null 2>&1 &
    tp=$!
    track "$tp"
    wait_for 3 comm_is "$tp" busy
    if [ "$(comm_of "$tp")" != "busy" ]; then bad "$label: the wrapper did not leave a busy process"; kill -9 "$tp" 2>/dev/null; return; fi
    n0=$(nice_of "$tp"); i0=$(ionice_of "$tp"); a0=$(affinity_of "$tp"); p0=$(policy_of "$tp")
    "$SPAWN" "$BIN" -l 10 -p "$tp" > /dev/null 2>&1 &
    lp=$!
    track "$lp"
    if saw_stopped "$tp" 5; then ok "$label: the target is limited"; else bad "$label: the target was never stopped"; fi
    n1=$(nice_of "$tp"); i1=$(ionice_of "$tp"); a1=$(affinity_of "$tp"); p1=$(policy_of "$tp")
    check "$label: nice value untouched ($n0)" [ "$n0" = "$n1" ]
    check "$label: I/O class untouched" [ "$i0" = "$i1" ]
    check "$label: CPU affinity untouched" [ "$a0" = "$a1" ]
    check "$label: scheduling policy untouched" [ "$p0" = "$p1" ]
    end_limiter "$lp"
    check "$label: the limiter exits 0 on SIGTERM" [ "$RC" -eq 0 ]
    sleep 0.1
    if not_stopped "$tp"; then ok "$label: the target is not left stopped"; else bad "$label: target left stopped"; fi
    kill -9 "$tp"; wait "$tp" 2>/dev/null
}

if works nice -n 1 true; then
    attach_case "nice -n 10" nice -n 10
else
    skip "nice is not usable here"
fi
if works ionice -c3 true && works ionice -p $$; then
    attach_case "ionice -c3" ionice -c3
    attach_case "ionice -c2 -n7" ionice -c2 -n7
else
    skip "ionice is not usable here"
fi
if works taskset -c 0 true && works taskset -p $$; then
    attach_case "taskset -c 0" taskset -c 0
else
    skip "taskset is not usable here"
fi
if works chrt -b 0 true && works chrt -p $$; then
    attach_case "chrt -b (SCHED_BATCH)" chrt -b 0
    if works chrt -i 0 true; then attach_case "chrt -i (SCHED_IDLE)" chrt -i 0; fi
else
    skip "chrt is not usable here"
fi
if works nice -n 1 true && works ionice -c3 true && works taskset -c 0 true; then
    attach_case "nice + ionice + taskset" nice -n 5 ionice -c3 taskset -c 0
fi

# ---------------------------------------------------------------- command mode through a wrapper
# command_case LABEL WRAPPER... : "cpulimit -l N WRAPPER... command"
command_case() {
    label=$1; shift
    "$BIN" -l 100 "$@" sh -c 'exit 7' > /dev/null 2>&1
    check "$label: the command's exit status (7) is returned" [ $? -eq 7 ]
    "$BIN" -l 100 "$@" sh -c 'exit 0' > /dev/null 2>&1
    check "$label: exit status 0 is returned" [ $? -eq 0 ]
    "$SPAWN" "$BIN" -l 10 "$@" "$BUSY" > /dev/null 2>&1 &
    lp=$!
    track "$lp"
    tp=$(busy_below "$lp" 4)
    if [ -z "$tp" ]; then bad "$label: no busy process under the limiter"; end_limiter "$lp"; return; fi
    track "$tp"
    check "$label: the busy process is the limiter's own child (the wrapper exec'd)" [ "$(children_of "$lp" | head -n 1)" = "$tp" ]
    if saw_stopped "$tp" 5; then ok "$label: the command is limited"; else bad "$label: the command was never stopped"; fi
    end_limiter "$lp"
    check "$label: SIGTERM ends the limiter with 0" [ "$RC" -eq 0 ]
    sleep 0.1
    kill -9 "$tp" 2>/dev/null; wait "$tp" 2>/dev/null
}

if works nice -n 1 true; then command_case "nice -n 5" nice -n 5; else skip "nice"; fi
if works ionice -c3 true; then command_case "ionice -c3" ionice -c3; else skip "ionice"; fi
if works taskset -c 0 true; then command_case "taskset -c 0" taskset -c 0; else skip "taskset"; fi
if works chrt -b 0 true; then command_case "chrt -b 0" chrt -b 0; else skip "chrt"; fi
command_case "env X=1" env X=1
if works nohup true; then command_case "nohup" nohup; else skip "nohup"; fi

# ---------------------------------------------------------------- -i through wrappers that fork
# tree_case LABEL command...: the busy loop is a grandchild or deeper and only -i reaches it
tree_case() {
    label=$1; shift
    "$SPAWN" "$BIN" -l 10 -i "$@" > /dev/null 2>&1 &
    lp=$!
    track "$lp"
    tp=$(busy_below "$lp" 4)
    if [ -z "$tp" ]; then bad "$label: no busy process under the limiter"; end_limiter "$lp"; return; fi
    track "$tp"
    depth=0; p=$tp
    while [ "$p" != "$lp" ] && [ "$depth" -lt 10 ]; do
        p=$(sed -n 's/^.*) . \([0-9][0-9]*\) .*/\1/p' "/proc/$p/stat" 2>/dev/null)
        depth=$((depth + 1))
    done
    check "$label: the busy process is below the limiter (depth $depth)" [ "$p" = "$lp" ]
    if saw_stopped "$tp" 6; then ok "$label: -i limits the descendant"; else bad "$label: the descendant was never stopped"; fi
    all=$(descendants_of "$lp")
    end_limiter "$lp"
    check "$label: the limiter exits 0" [ "$RC" -eq 0 ]
    sleep 0.1
    left=""
    for d in $all; do
        if alive "$d" && [ "$(state "$d")" = "T" ]; then left="$left $d"; fi
    done
    check "$label: no descendant is left stopped" [ -z "$left" ]
    for d in $all; do kill -9 "$d" 2>/dev/null; done
    wait 2>/dev/null
}

if works timeout 5 true; then tree_case "timeout" timeout 60 "$BUSY"; else skip "timeout"; fi
if works setsid -w true; then tree_case "setsid -w" setsid -w "$BUSY"; else skip "setsid"; fi
tree_case "sh, nice and a background job" sh -c 'nice -n 5 "$1" & wait' sh "$BUSY"
tree_case "sh -c exec chain" sh -c 'sh -c "exec \$0" "$1"' sh "$BUSY"

# ---------------------------------------------------------------- the limiter itself under wrappers
# limiter_case LABEL WRAPPER...: "WRAPPER... cpulimit -l 10 -p busy"
limiter_case() {
    label=$1; shift
    start_busy
    "$SPAWN" "$@" "$BIN" -l 10 -p "$BP" > /dev/null 2>&1 &
    lp=$!
    track "$lp"
    if saw_stopped "$BP" 6; then ok "$label: the limiter works"; else bad "$label: the target was never stopped"; fi
    end_limiter "$lp"
    check "$label: it exits 0 on SIGTERM" [ "$RC" -eq 0 ]
    sleep 0.1
    if not_stopped "$BP"; then ok "$label: the target is not left stopped"; else bad "$label: target left stopped"; fi
    kill -9 "$BP"; wait "$BP" 2>/dev/null
}
if works nice -n 1 true; then limiter_case "nice -n 5 cpulimit" nice -n 5; fi
if works ionice -c3 true; then limiter_case "ionice -c3 cpulimit" ionice -c3; fi
if works taskset -c 0 true; then limiter_case "taskset -c 0 cpulimit" taskset -c 0; fi
if works nice -n 1 true && works ionice -c3 true; then limiter_case "nice + ionice cpulimit" nice -n 5 ionice -c3; fi

# --no-boost leaves the priority alone; without it cpulimit tries to raise it and says what happened
if works nice -n 1 true; then
    want=$((base_nice + 5)); [ "$want" -gt 19 ] && want=19
    start_busy
    "$SPAWN" nice -n 5 "$BIN" -v --no-boost -l 20 -p "$BP" > "$WORK/nb.out" 2>&1 &
    lp=$!
    track "$lp"
    sleep 1
    check "--no-boost: the limiter's nice value stays $want" [ "$(nice_of "$lp")" = "$want" ]
    end_limiter "$lp"
    case $(cat "$WORK/nb.out") in
    *"Priority changed"* | *"Cannot change priority"*) bad "--no-boost: cpulimit still touched or reported its priority" ;;
    *) ok "--no-boost: it does not try to change its priority" ;;
    esac
    kill -9 "$BP"; wait "$BP" 2>/dev/null

    start_busy
    "$SPAWN" nice -n 5 "$BIN" -v -l 20 -p "$BP" > "$WORK/boost.out" 2>&1 &
    lp=$!
    track "$lp"
    sleep 1
    got=$(nice_of "$lp")
    case $(cat "$WORK/boost.out") in
    *"Priority changed to "*)
        said=$(sed -n 's/^Priority changed to \(-\{0,1\}[0-9][0-9]*\).*/\1/p' "$WORK/boost.out" | head -n 1)
        check "boost: it reports the priority it set ($said) and has it" [ "$said" = "$got" ]
        check "boost: the new priority is better than $want" [ "$got" -lt "$want" ]
        ;;
    *"Cannot change priority"*)
        check "boost refused: the nice value stays $want" [ "$got" = "$want" ]
        ;;
    *) bad "boost: neither 'Priority changed' nor 'Cannot change priority' was printed with -v" "$(head -c 300 "$WORK/boost.out")" ;;
    esac
    end_limiter "$lp"
    kill -9 "$BP"; wait "$BP" 2>/dev/null
fi

# ---------------------------------------------------------------- a wrapper ends the limiter
if works timeout 5 true; then
    start_busy
    "$SPAWN" timeout -s TERM 2 "$BIN" -l 10 -p "$BP" > /dev/null 2>&1 &
    tl=$!
    track "$tl"
    saw_stopped "$BP" 1 && ok "timeout: the limiter is working before the timeout" || bad "timeout: the target was not limited"
    wait_for 5 gone "$tl"
    wait "$tl" 2>/dev/null
    rc=$?
    check "timeout: it ended the limiter (status 124)" [ "$rc" -eq 124 ]
    sleep 0.2
    if not_stopped "$BP"; then ok "timeout: SIGTERM from a wrapper does not strand the target"; else bad "timeout: target left stopped"; fi
    kill -9 "$BP"; wait "$BP" 2>/dev/null
else
    skip "timeout"
fi

# ---------------------------------------------------------------- the target dies while it is limited
if works timeout 5 true; then
    # timeout kills the busy loop with SIGKILL after 2 s, most likely while it is stopped
    "$SPAWN" timeout -s KILL 2 "$BUSY" > /dev/null 2>&1 &
    wp=$!
    track "$wp"
    tp=$(busy_below "$wp" 4)
    if [ -z "$tp" ]; then
        bad "target killed by a wrapper: no busy process"
    else
        track "$tp"
        "$SPAWN" "$BIN" -v -l 10 -p "$tp" > "$WORK/die.out" 2>&1 &
        lp=$!
        track "$lp"
        saw_stopped "$tp" 1 && ok "target killed by a wrapper: it is limited first" || bad "target killed by a wrapper: never limited"
        if wait_for 8 gone "$lp"; then ok "target killed by a wrapper: the limiter leaves by itself"; else bad "target killed by a wrapper: the limiter is still running"; kill -9 "$lp"; fi
        wait "$lp" 2>/dev/null
        check "target killed by a wrapper: exit status 0" [ $? -eq 0 ]
    fi
    kill -9 "$wp" 2>/dev/null; wait "$wp" 2>/dev/null
fi

# ---------------------------------------------------------------- verbose output names the wrapped command
OUT=$("$BIN" -v -l 100 env CPULIMIT_WRAPPER_TEST=1 sh -c 'exit 0' 2>&1)
case $OUT in
*"Running command: 'env CPULIMIT_WRAPPER_TEST=1 sh -c exit 0'"*) ok "-v prints the wrapper and its arguments" ;;
*) bad "-v did not print the command line" "$OUT" ;;
esac

# ---------------------------------------------------------------- a limiter without privilege (needs root to set up)
NOBODY="setpriv --reuid=65534 --regid=65534 --clear-groups"
if [ "$(id -u)" = "0" ] && works $NOBODY sh -c "test -x '$BIN' && test -x '$BUSY' && test -x '$SPAWN'"; then
    chmod 711 "$WORK"
    mkdir "$WORK/nrun" && chown 65534 "$WORK/nrun" && chmod 700 "$WORK/nrun"
    # a target of the same unprivileged user: limited; raising the limiter's priority is refused
    "$SPAWN" $NOBODY "$BUSY" > /dev/null 2>&1 &
    np=$!
    track "$np"
    "$SPAWN" $NOBODY env XDG_RUNTIME_DIR="$WORK/nrun" "$BIN" -v -l 10 -p "$np" > "$WORK/nobody.out" 2>&1 &
    lp=$!
    track "$lp"
    saw_stopped "$np" 6 && ok "unprivileged limiter: it limits its own user's process" || bad "unprivileged limiter: the target was never stopped"
    check "unprivileged limiter: it runs as that user" [ "$(awk '/^Uid:/ {print $2}' "/proc/$lp/status")" = "65534" ]
    end_limiter "$lp"
    case $(cat "$WORK/nobody.out") in
    *"Cannot change priority"*) ok "unprivileged limiter: -v says the priority could not be raised" ;;
    *) bad "unprivileged limiter: no 'Cannot change priority' with -v" "$(head -c 300 "$WORK/nobody.out")" ;;
    esac
    kill -9 "$np"; wait "$np" 2>/dev/null

    # a target of another user (root): refused, and nothing is left stopped
    start_busy
    OUT=$("$SPAWN" $NOBODY env XDG_RUNTIME_DIR="$WORK/nrun" "$BIN" -l 10 -p "$BP" 2>&1)
    case $OUT in
    *"aren't allowed to control it"*) ok "unprivileged limiter: another user's process is refused with a reason" ;;
    *) bad "unprivileged limiter: no 'aren't allowed' message for root's process" "$OUT" ;;
    esac
    not_stopped "$BP" && ok "...and that process is untouched" || bad "...that process was stopped"
    kill -9 "$BP"; wait "$BP" 2>/dev/null
else
    skip "unprivileged limiter (needs root and setpriv to become another user)"
fi

# ---------------------------------------------------------------- the share holds for niced and pinned targets
measure_wrapped() { # measure_wrapped LIMIT SECONDS WRAPPER... -> percent of one CPU used
    lim=$1; secs=$2; shift 2
    "$SPAWN" "$@" "$BUSY" > /dev/null 2>&1 &
    mp=$!
    track "$mp"
    "$SPAWN" "$BIN" -l "$lim" -p "$mp" > /dev/null 2>&1 &
    ml=$!
    track "$ml"
    sleep 1.5
    t0=$(ticks "$mp"); w0=$(date +%s.%N 2>/dev/null || date +%s)
    sleep "$secs"
    t1=$(ticks "$mp"); w1=$(date +%s.%N 2>/dev/null || date +%s)
    end_limiter "$ml"
    kill -9 "$mp"; wait "$mp" 2>/dev/null
    awk -v a="$t0" -v b="$t1" -v x="$w0" -v y="$w1" 'BEGIN { printf "%.0f", (b - a) / 100 / (y - x) * 100 }'
}
if [ -n "$CPULIMIT_SKIP_TIMING" ]; then
    skip "limit accuracy under wrappers (CPULIMIT_SKIP_TIMING set)"
else
    hz=$(getconf CLK_TCK 2>/dev/null || echo 100)
    if [ "$hz" != "100" ]; then
        skip "limit accuracy under wrappers (clock ticks $hz/s, the bounds assume 100)"
    else
        if works nice -n 1 true; then
            got=$(measure_wrapped 30 6 nice -n 10)
            if [ "$got" -ge 18 ] && [ "$got" -le 42 ]; then ok "limit 30% holds for a niced target: measured ${got}%"; else bad "limit 30% for nice -n 10: measured ${got}% (accepted 18-42%)"; fi
        fi
        if works taskset -c 0 true; then
            got=$(measure_wrapped 30 6 taskset -c 0)
            if [ "$got" -ge 18 ] && [ "$got" -le 42 ]; then ok "limit 30% holds for a target pinned to CPU 0: measured ${got}%"; else bad "limit 30% for taskset -c 0: measured ${got}% (accepted 18-42%)"; fi
        fi
    fi
fi

summary
