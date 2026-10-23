#!/bin/sh
# Build cpulimit as it was at a git ref and print the path of the binary:
#
#   sh bench/build_ref.sh REF DIR     (REF: any commit-ish; DIR: a new worktree, removed first if present)
#
# Understands both layouts the history has: the modern one (a Makefile at the top level, binary in
# build/) and the original one (src/Makefile, binary in src/). Progress goes to stderr, so that
#   BASELINE=$(sh bench/build_ref.sh 53e5dc6 ../baseline) make bench
# works. Needs the ref to be present (a shallow clone does not have old commits).
set -eu
ref=${1:?usage: build_ref.sh REF DIR}
dir=${2:?usage: build_ref.sh REF DIR}
if ! git rev-parse --verify --quiet "$ref^{commit}" >/dev/null; then
    echo "build_ref: unknown git ref '$ref' (a shallow clone lacks old commits: fetch with depth 0)" >&2
    exit 2
fi
if [ -e "$dir" ]; then
    git worktree remove --force "$dir" >&2 2>/dev/null || rm -rf "$dir"
fi
git worktree add --detach "$dir" "$ref" >&2
if [ -f "$dir/Makefile" ] && [ ! -f "$dir/src/Makefile" ]; then
    make -C "$dir" all CFLAGS="-O2" >&2
    candidates="$dir/build/cpulimit $dir/cpulimit"
elif [ -f "$dir/src/Makefile" ]; then
    make -C "$dir/src" CFLAGS="-O2" >&2
    candidates="$dir/src/cpulimit"
else
    echo "build_ref: '$ref' has neither a top-level Makefile nor src/Makefile" >&2
    exit 2
fi
for bin in $candidates; do
    if [ -x "$bin" ]; then
        echo "$bin"
        exit 0
    fi
done
echo "build_ref: built '$ref' but found no cpulimit binary in: $candidates" >&2
exit 2
