#!/bin/sh
# Mutation check of the unit tests: re-introduce one defect at a time into a scratch copy of the
# tree and require that the unit tests (test/run_tests.sh with SKIP_INTEGRATION=1) fail.
# A SURVIVED line is a gap in the tests, or an equivalent mutant (which cannot change behavior);
# the list below notes the known equivalent ones.
#   sh scripts/mutation.sh [mutants-file]       default: scripts/mutants.txt
# File format, one per line:   description | source file | sed expression
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
list=${1:-$root/scripts/mutants.txt}
work=$(mktemp -d) || exit 2
trap 'rm -rf "$work"' EXIT INT TERM
killed=0; survived=0; skipped=0
while IFS='|' read -r desc file expr; do
    case $desc in ''|'#'*) continue ;; esac
    desc=$(printf '%s' "$desc" | sed 's/[[:space:]]*$//')
    file=$(printf '%s' "$file" | tr -d ' ')
    expr=$(printf '%s' "$expr" | sed 's/^[[:space:]]*//')
    rm -rf "$work/t"; mkdir "$work/t"
    (cd "$root" && cp -r Makefile src test "$work/t/")
    cp "$work/t/$file" "$work/t/$file.orig"
    sed -i "$expr" "$work/t/$file"
    if cmp -s "$work/t/$file" "$work/t/$file.orig"; then
        printf 'SKIPPED   %s (expression changed nothing)\n' "$desc"; skipped=$((skipped + 1)); continue
    fi
    rm -f "$work/t/$file.orig"
    if (cd "$work/t" && make -s check SKIP_INTEGRATION=1 >"$work/log" 2>&1); then
        printf 'SURVIVED  %s\n' "$desc"; survived=$((survived + 1))
    else
        printf 'killed    %s\n' "$desc"; killed=$((killed + 1))
    fi
done < "$list"
printf '\n%d killed, %d survived, %d skipped\n' "$killed" "$survived" "$skipped"
[ "$survived" -eq 0 ]
