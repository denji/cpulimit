#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Line and branch coverage of src/ from the whole test suite, using gcov only.
#   CC=gcc BUILD=build/cov sh tools/coverage.sh
# Prints one row per source file and writes the uncovered lines to $BUILD/uncovered.txt.
# Set COV_MIN_LINES=NN to fail when the total line coverage is below NN percent.
set -eu
CC=${CC:-gcc}
BUILD=${BUILD:-build/cov}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
rm -rf "$BUILD"
mkdir -p "$BUILD"
# -O0 and no inlining keep the line numbers honest; WERROR stays off, coverage is not a lint run.
make -s CC="$CC" BUILD="$BUILD" CFLAGS="-O0 -g --coverage -fprofile-update=atomic -DCL_COVERAGE" \
    LDFLAGS="--coverage" all "$BUILD/test/busy" "$BUILD/test/spawn" \
    $(for t in process_iterator util limiter procfs_parse group list cli_parse lock; do printf '%s ' "$BUILD/test/${t}_test"; done) >/dev/null
BUILD="$BUILD" sh test/run_tests.sh >"$BUILD/test.log" 2>&1 || { tail -20 "$BUILD/test.log"; echo "coverage: tests failed" >&2; exit 1; }
BUILD=$(cd "$BUILD" && pwd)
cd "$BUILD"
# gcov merges counts for a source across objects only per invocation, so run it once per object
# and merge the .gcov outputs by taking the maximum count per line.
rm -f ./*.gcov
# (src/platform/<os>/iterator.c are #included by src/platform/iterator.c: gcov reports them there)
for src in $(cd "$root" && find src -name '*.c' | grep -v '^src/platform/[a-z]*/iterator\.c$' | sort); do
    rel=${src#src/}; rel=${rel%.c}            # e.g. core/limiter
    key=$(printf '%s' "$rel" | tr / _)         # e.g. core_limiter
    n=0
    for obj in "./$rel" "test/gt_$(basename "$rel")"; do
        [ -f "$obj.gcno" ] || continue
        n=$((n + 1))
        # gcov resolves the source relative to the compile directory, which is the repository root
        (cd "$root" && gcov -b -c -o "$BUILD/$obj.o" -t "src/$rel.c") >"$key.$n.gcov" 2>/dev/null || true
    done
done
python3 - "$root" <<'PY'
import glob, os, re, sys
root = sys.argv[1]
tot_l = cov_l = tot_b = cov_b = 0
rows = []
unc = open("uncovered.txt", "w")
srcs = [p for p in glob.glob(root + "/src/**/*.c", recursive=True)
        if not re.search(r"/src/platform/[a-z]+/iterator\.c$", p)]
for src in sorted(srcs):
    base = os.path.relpath(src, root + "/src")[:-2]      # e.g. core/limiter
    key = base.replace("/", "_")
    lines = {}      # lineno -> max count
    branches = {}   # (lineno, idx) -> taken
    for g in glob.glob(key + ".*.gcov"):
        cur = None
        for raw in open(g, errors="replace"):
            m = re.match(r"\s*([0-9#=\-]+\*?):\s*(\d+):", raw)
            if m:
                cnt, ln = m.group(1).rstrip("*"), int(m.group(2))
                cur = ln
                if ln == 0 or cnt in ("-", "="):
                    continue
                v = 0 if cnt == "#####" else int(cnt)
                lines[ln] = max(lines.get(ln, 0), v)
                continue
            m = re.match(r"branch\s+(\d+)\s+(taken (\d+)|never executed)", raw)
            if m and cur:
                k = (cur, int(m.group(1)))
                t = int(m.group(3) or 0)
                branches[k] = max(branches.get(k, 0), t)
    if not lines:
        continue
    l_tot, l_cov = len(lines), sum(1 for v in lines.values() if v > 0)
    b_tot, b_cov = len(branches), sum(1 for v in branches.values() if v > 0)
    tot_l += l_tot; cov_l += l_cov; tot_b += b_tot; cov_b += b_cov
    rows.append((base + ".c", l_cov, l_tot, b_cov, b_tot))
    miss = [ln for ln, v in sorted(lines.items()) if v == 0]
    if miss:
        unc.write("%s: %s\n" % (base + ".c", ",".join(map(str, miss))))
def pct(a, b): return "%5.1f%%" % (100.0 * a / b) if b else "   n/a"
print("%-28s %14s %16s" % ("file", "lines", "branches"))
for n, lc, lt, bc, bt in rows:
    print("%-28s %6s %3d/%-4d %6s %3d/%-4d" % (n, pct(lc, lt), lc, lt, pct(bc, bt), bc, bt))
print("%-28s %6s %3d/%-4d %6s %3d/%-4d" % ("TOTAL", pct(cov_l, tot_l), cov_l, tot_l, pct(cov_b, tot_b), cov_b, tot_b))
mn = os.environ.get("COV_MIN_LINES")
if mn and tot_l and 100.0 * cov_l / tot_l < float(mn):
    sys.exit("coverage: %.1f%% of lines is below COV_MIN_LINES=%s" % (100.0 * cov_l / tot_l, mn))
PY
echo "uncovered lines: $BUILD/uncovered.txt"
