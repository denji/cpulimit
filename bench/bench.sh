#!/bin/sh
# Compare two cpulimit binaries (a baseline and a candidate) on the same machine, back to back.
#
#   sh bench/bench.sh BASELINE_BIN CANDIDATE_BIN BUSY_BIN [SECONDS]
#
# Three measurements, each reported per binary:
#   accuracy    CPU share a one-thread busy loop actually gets for -l 10/25/50/75/90
#   overhead    CPU time the limiter itself burns per second, and its system calls per second
#               (strace -c -f; needs strace, skipped otherwise), limiting one busy process
#   scaling     the same with -i and N idle child processes under the limited parent
# Numbers depend on the machine and its load; compare the two columns of one run, not runs
# of different machines. Needs /proc, so Linux only.
set -u
OLD=${1:?baseline binary}
NEW=${2:?candidate binary}
BUSY=${3:?busy binary (build/test/busy)}
SECS=${4:-8}
CHILDREN=${CHILDREN:-200}
HZ=$(getconf CLK_TCK)

# utime+stime (ticks) of a pid, parsed after the last ')' so a command name cannot confuse it
ticks() {
    awk '{ s=$0; sub(/^.*\) /, "", s); n=split(s, f, " "); print f[12] + f[13] }' "/proc/$1/stat" 2>/dev/null || echo 0
}

now_ms() { awk '{ printf "%d", $1 * 1000 }' /proc/uptime; }

cleanup() {
    kill -CONT $(jobs -p) 2>/dev/null
    kill $(jobs -p) 2>/dev/null
    wait 2>/dev/null
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

# run_case BIN LIMIT -> prints "share_percent limiter_cpu_percent"
run_case() {
    bin=$1; limit=$2; mode=$3
    if [ "$mode" = children ]; then
        sh -c 'i=0; while [ $i -lt '"$CHILDREN"' ]; do sleep 1000 & i=$((i+1)); done; exec '"$BUSY"'' &
        target=$!
        sleep 1
        "$bin" -p $target -l "$limit" -i >/dev/null 2>&1 &
    else
        "$BUSY" &
        target=$!
        "$bin" -p $target -l "$limit" >/dev/null 2>&1 &
    fi
    lim=$!
    sleep 2                                   # settle: the controller needs a few cycles
    t0=$(now_ms); a0=$(ticks $target); l0=$(ticks $lim)
    sleep "$SECS"
    t1=$(now_ms); a1=$(ticks $target); l1=$(ticks $lim)
    kill -CONT $target 2>/dev/null
    kill $lim 2>/dev/null; wait $lim 2>/dev/null
    pkill -P $target 2>/dev/null
    kill -CONT $target 2>/dev/null; kill $target 2>/dev/null; wait $target 2>/dev/null
    awk -v a="$((a1 - a0))" -v l="$((l1 - l0))" -v ms="$((t1 - t0))" -v hz="$HZ" \
        'BEGIN { w = ms / 1000.0; printf "%.1f %.2f", 100.0 * a / hz / w, 100.0 * l / hz / w }'
}

# syscalls per second made by the limiter (strace -c -f summary total)
syscalls() {
    bin=$1
    command -v strace >/dev/null 2>&1 || { echo "n/a"; return; }
    "$BUSY" &
    target=$!
    log=$(mktemp)
    strace -f -c -o "$log" "$bin" -p $target -l 50 >/dev/null 2>&1 &
    sp=$!
    sleep "$SECS"
    pkill -TERM -P $sp 2>/dev/null          # the traced limiter is strace's child
    wait $sp 2>/dev/null
    kill -CONT $target 2>/dev/null; kill $target 2>/dev/null; wait $target 2>/dev/null
    awk -v s="$SECS" '/ total$/ { c = (NF >= 6) ? $(NF-2) : $(NF-1); printf "%d", c / s; found = 1 } END { if (!found) printf "n/a" }' "$log"
    rm -f "$log"
}

echo "host: $(uname -sr), $(nproc) CPU(s), CLK_TCK=$HZ, ${SECS}s per case"
echo
echo "accuracy (share of one CPU received by a 1-thread busy loop; ideal = limit)"
printf '%-8s %12s %12s\n' limit baseline candidate
for l in 10 25 50 75 90; do
    o=$(run_case "$OLD" $l single); n=$(run_case "$NEW" $l single)
    printf '%-8s %11s%% %11s%%\n' "$l%" "${o% *}" "${n% *}"
done
echo
echo "overhead of the limiter itself, limiting one busy process at 50%"
o=$(run_case "$OLD" 50 single); n=$(run_case "$NEW" 50 single)
printf '%-28s %12s %12s\n' metric baseline candidate
printf '%-28s %11s%% %11s%%\n' "limiter CPU (% of one CPU)" "${o#* }" "${n#* }"
printf '%-28s %12s %12s\n' "syscalls per second" "$(syscalls "$OLD")" "$(syscalls "$NEW")"
echo
echo "scaling: -i with $CHILDREN idle children, 50%"
o=$(run_case "$OLD" 50 children); n=$(run_case "$NEW" 50 children)
printf '%-28s %12s %12s\n' metric baseline candidate
printf '%-28s %11s%% %11s%%\n' "limiter CPU (% of one CPU)" "${o#* }" "${n#* }"
printf '%-28s %11s%% %11s%%\n' "busy child share" "${o% *}" "${n% *}"
