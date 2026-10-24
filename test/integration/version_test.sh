#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# The version appears in three places that must agree: the binary (src/cli/cli.c), the man page and
# the newest CHANGELOG entry. A release with a stale one is a bug nothing else would catch.
set -eu
cd "$(dirname "$0")/../.."
BUILD=${BUILD:-build}
code=$(sed -n 's/^#define VERSION "\(.*\)"$/\1/p' src/cli/cli.c)
bin=$("$BUILD/cpulimit" -V | sed -n '1s/^[^0-9]*//p')
man=$(sed -n '1s/^\.TH CPULIMIT 1 "[^"]*" "cpulimit \([^"]*\)".*$/\1/p' man/cpulimit.1)
log=$(sed -n 's/^## \[\([0-9][^]]*\)\].*$/\1/p' CHANGELOG.md | head -n 1)
fail=0
for v in "bin=$bin" "man=$man" "changelog=$log"; do
    [ "${v#*=}" = "$code" ] || { echo "version mismatch: ${v%%=*} says '${v#*=}', src/cli/cli.c says '$code'" >&2; fail=1; }
done
[ -n "$code" ] || { echo "no VERSION in src/cli/cli.c" >&2; fail=1; }
# a tag given by CI (GITHUB_REF_NAME) must name this version too
if [ -n "${RELEASE_TAG:-}" ]; then
    [ "${RELEASE_TAG#v}" = "$code" ] || { echo "tag '$RELEASE_TAG' does not match version '$code'" >&2; fail=1; }
fi
[ "$fail" -eq 0 ] && echo "version $code consistent (binary, man page, changelog)"
exit "$fail"
