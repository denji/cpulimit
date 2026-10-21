#!/bin/sh
# Run the verification harnesses.
#   sh scripts/verif.sh sim    random testing of the harness properties (any C compiler, no CBMC)
#   sh scripts/verif.sh cbmc   bounded model checking of the same harnesses (needs cbmc)
set -eu
cd "$(dirname "$0")/.."
mode=${1:-sim}
CC=${CC:-cc}
OUT=${BUILD:-build}/verif
mkdir -p "$OUT"
N=${VERIF_SAMPLES:-2000000}
# harness:sources
SET="${VERIF_ONLY:-verify_limiter verify_ticks verify_parse_stat}"
status=0
for h in $SET; do
    case $h in
    verify_limiter) src="src/limiter.c"; unwind=3 ;;
    verify_ticks) src="src/procfs_parse.c"; unwind=3 ;;
    verify_parse_stat) src="src/procfs_parse.c"; unwind=50 ;;   # MAXLEN 48 + 2
    *) echo "unknown harness $h" >&2; exit 2 ;;
    esac
    case $mode in
    sim)
        $CC -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra \
            -D_GNU_SOURCE -Isrc -Iverif -o "$OUT/$h" "verif/$h.c" $src -lm
        "$OUT/$h" "$N" || status=1
        ;;
    cbmc)
        # unwinding covers the 64-byte buffer plus the digit loops; --unwinding-assertions makes an
        # insufficient bound a failure instead of a silent partial result
        echo "== $h"
        # A virtual-memory ulimit is only applied when asked for (CBMC_MEM_KB): address-space limits
        # make C++ programs fail in ways that look like crashes. Line-buffered output keeps what
        # cbmc printed before it died, and its exit status is reported, because a process killed
        # by a signal prints nothing (the first conclusive CI run ended silently mid-solve).
        if [ -n "${CBMC_MEM_KB:-}" ]; then ulimit -v "$CBMC_MEM_KB" 2>/dev/null || true; fi
        stdbuf -oL -eL timeout "${CBMC_TIMEOUT:-1500}" cbmc "verif/$h.c" $src -DVERIF_CBMC=1 -D_GNU_SOURCE -Isrc -Iverif \
            --unwind $unwind --unwinding-assertions \
            --bounds-check --pointer-check --div-by-zero-check \
            --signed-overflow-check --unsigned-overflow-check --conversion-check \
            --pointer-overflow-check --undefined-shift-check --float-overflow-check --nan-check \
            --object-bits 12 && rc=0 || rc=$?
        if [ "$rc" -ne 0 ]; then
            echo "cbmc $h: exit status $rc (10 = property violated, 6 = unsupported/internal error, 124 = timeout, 137 = SIGKILL (out of memory?), 139 = SIGSEGV)"
            status=1
        fi
        ;;
    *) echo "usage: $0 sim|cbmc" >&2; exit 2 ;;
    esac
done
exit $status
