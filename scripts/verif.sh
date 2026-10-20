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
SET="verify_limiter:src/limiter.c verify_ticks:src/procfs_parse.c verify_parse_stat:src/procfs_parse.c"
status=0
for entry in $SET; do
    h=${entry%%:*}; src=${entry#*:}
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
        timeout "${CBMC_TIMEOUT:-1500}" cbmc "verif/$h.c" $src -D_GNU_SOURCE -Isrc -Iverif \
            --unwind 70 --unwinding-assertions \
            --bounds-check --pointer-check --div-by-zero-check \
            --signed-overflow-check --unsigned-overflow-check --conversion-check \
            --pointer-overflow-check --undefined-shift-check --float-overflow-check --nan-check \
            --object-bits 12 || status=1
        ;;
    *) echo "usage: $0 sim|cbmc" >&2; exit 2 ;;
    esac
done
exit $status
