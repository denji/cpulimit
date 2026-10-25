#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Format the C sources with clang-format using .clang-format.
#     sh .format.sh          rewrite the files
#     sh .format.sh --check  change nothing, list files that differ, exit 1 if any
# CLANG_FORMAT selects the binary (CI uses clang-format-18; other major versions format
# differently, so a mismatch is reported instead of silently rewriting the tree).
set -eu
cd "$(dirname "$0")"
CF=${CLANG_FORMAT:-clang-format}
command -v "$CF" >/dev/null 2>&1 || { echo "$CF not found" >&2; exit 2; }
files=$(git ls-files 'src/*.c' 'src/*.h' 'test/*.c' 'test/*.h' 'fuzz/*.c' 'verif/*.c' 'verif/*.h')
if [ "${1:-}" = --check ]; then
    bad=0
    for f in $files; do
        if ! "$CF" "$f" | cmp -s - "$f"; then
            echo "needs formatting: $f"
            bad=1
        fi
    done
    exit "$bad"
fi
# shellcheck disable=SC2086
"$CF" -i $files
