#!/bin/sh
# Load and memory tests for the limiter. Not part of "make check" (it takes ~1 minute);
#   BUILD=build sh test/stress_test.sh        make stress
#
#   churn    -i over a parent that keeps forking short-lived children for LOAD_SECS seconds:
#            the limiter must stay alive, its resident memory must not grow, and no process of
#            the tree may be left stopped afterwards (invariant S1)
#   wide     -i over WIDE_CHILDREN idle children: all of them are found and are limited together
#   valgrind the limiter in attach and command mode under valgrind --leak-check=full
#            (skipped when valgrind is missing)
BUILD=${BUILD:-build}
LOAD_SECS=${LOAD_SECS:-15}
WIDE_CHILDREN=${WIDE_CHILDREN:-300}
CPULIMIT=$BUILD/cpulimit
BUSY=$BUILD/test/busy
fails=0
checks=0

ok()   { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL %s\n' "$1"; }
check() { desc=$1; shift; if "$@"; then ok "$desc"; else fail "$desc"; fi; }

tmp=$(mktemp -d) || exit 2
trap 'kill -CONT $(jobs -p) 2>/dev/null; kill $(jobs -p) 2>/dev/null; wait 2>/dev/null; rm -rf "$tmp"' EXIT
trap 'exit 130' INT TERM

rss_kb() { awk '/^VmRSS:/ { print $2 }' "/proc/$1/status" 2>/dev/null; }
state_of() { awk '{ s=$0; sub(/^.*\) /, "", s); print substr(s, 1, 1) }' "/proc/$1/stat" 2>/dev/null; }
# all descendants of $1 (including $1), one pid per line
tree() { echo "$1"; for c in $(pgrep -P "$1" 2>/dev/null); do tree "$c"; done; }

echo "churn: $LOAD_SECS s of fork/exit under -i"
sh -c 'end=$(( $(date +%s) + '"$LOAD_SECS"' + 4 )); while [ $(date +%s) -lt $end ]; do (i=0; while [ $i -lt 3000 ]; do i=$((i+1)); done) & (sleep 0.05) & wait; done' &
parent=$!
"$CPULIMIT" -p $parent -l 40 -i >"$tmp/churn.out" 2>&1 &
lim=$!
sleep 3
rss0=$(rss_kb $lim)
sleep "$LOAD_SECS"
rss1=$(rss_kb $lim)
check "limiter survived the churn" kill -0 $lim
# 1 MiB of slack: allocator arenas settle, a leak per cycle would add tens of MiB in this time
check "limiter memory is flat (${rss0:-?} kB -> ${rss1:-?} kB)" sh -c '[ -n "$1" ] && [ -n "$2" ] && [ "$2" -le $(( $1 + 1024 )) ]' _ "$rss0" "$rss1"
stopped=0
for p in $(tree $parent); do [ "$(state_of "$p")" = T ] && stopped=$((stopped + 1)); done
echo "       (processes currently in a stop slice: $stopped - normal while limiting)"
kill -TERM $lim 2>/dev/null; wait $lim 2>/dev/null
sleep 0.3
stopped=0
for p in $(tree $parent); do [ "$(state_of "$p")" = T ] && stopped=$((stopped + 1)); done
check "no process of the tree is left stopped after SIGTERM" [ "$stopped" -eq 0 ]
kill $parent 2>/dev/null; wait $parent 2>/dev/null
pkill -P $parent 2>/dev/null

echo "wide: -i over $WIDE_CHILDREN children"
sh -c 'i=0; while [ $i -lt '"$WIDE_CHILDREN"' ]; do sleep 600 & i=$((i+1)); done; wait' &
wparent=$!
sleep 2
"$CPULIMIT" -p $wparent -l 30 -i -v >"$tmp/wide.out" 2>&1 &
lim=$!
sleep 4
found=$(tree $wparent | wc -l)
check "all $WIDE_CHILDREN children plus the parent exist (found $found)" [ "$found" -ge $((WIDE_CHILDREN + 1)) ]
check "limiter still alive with $WIDE_CHILDREN children" kill -0 $lim
kill -TERM $lim 2>/dev/null; wait $lim 2>/dev/null
sleep 0.3
stopped=0
for p in $(tree $wparent); do [ "$(state_of "$p")" = T ] && stopped=$((stopped + 1)); done
check "no child left stopped" [ "$stopped" -eq 0 ]
kill -TERM $(tree $wparent) 2>/dev/null; wait $wparent 2>/dev/null

if command -v valgrind >/dev/null 2>&1; then
    echo "valgrind"
    VG="valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=99 -q"
    "$BUSY" &
    t=$!
    $VG "$CPULIMIT" -p $t -l 50 >"$tmp/vg1.out" 2>&1 &
    vg=$!
    sleep 6
    kill -TERM $(pgrep -P $$ -f "cpulimit -p $t") 2>/dev/null
    kill -TERM $vg 2>/dev/null
    wait $vg 2>/dev/null; rc=$?
    kill -CONT $t 2>/dev/null; kill $t 2>/dev/null; wait $t 2>/dev/null
    check "attach mode: no leaks or invalid accesses (exit $rc)" [ "$rc" -ne 99 ]
    $VG "$CPULIMIT" -l 50 -- sh -c 'i=0; while [ $i -lt 20000 ]; do i=$((i+1)); done' >"$tmp/vg2.out" 2>&1
    rc=$?
    check "command mode: no leaks or invalid accesses (exit $rc)" [ "$rc" -ne 99 ]
    [ -s "$tmp/vg1.out" ] && [ "$rc" -eq 99 ] && cat "$tmp/vg1.out" "$tmp/vg2.out"
else
    echo "valgrind: not installed, skipped"
fi

printf '\n%d check(s), %d failed\n' "$checks" "$fails"
[ "$fails" -eq 0 ]
