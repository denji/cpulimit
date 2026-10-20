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
        # an out-of-memory CBMC must die alone, not take the CI runner with it
        ulimit -v "${CBMC_MEM_KB:-12000000}" 2>/dev/null || true
        timeout "${CBMC_TIMEOUT:-1500}" cbmc "verif/$h.c" $src -D_GNU_SOURCE -Isrc -Iverif \
            --unwind $unwind --unwinding-assertions \
            --bounds-check --pointer-check --div-by-zero-check \
            --signed-overflow-check --unsigned-overflow-check --conversion-check \
            --pointer-overflow-check --undefined-shift-check --float-overflow-check --nan-check \
            --object-bits 12 || status=1
        ;;
    *) echo "usage: $0 sim|cbmc" >&2; exit 2 ;;
    esac
done
exit $status
