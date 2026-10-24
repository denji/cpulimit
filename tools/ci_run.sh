#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Run a command; when it fails, repeat the interesting part of its output as a GitHub error
# annotation:   sh tools/ci_run.sh LABEL command args...   (POSIX sh: runs on the BSD VMs too)
# Annotations can be read through the checks API without logging in, which the raw job log
# cannot, so a failure can be diagnosed without opening the Actions UI. Full output is still
# printed to the job log.
label=$1; shift
log=$(mktemp)
# no pipefail in POSIX sh: the command writes its status to a file, tee keeps the output live
{ "$@" 2>&1; echo $? >"$log.rc"; } | tee "$log"
rc=$(cat "$log.rc")
if [ "$rc" -ne 0 ]; then
    # error and FAIL lines first (what broke), then the last lines (where it stopped)
    {
        grep -nE ': FAILURE|Runtime|Generated [0-9]+ VCC|Solving with|SAT checker|variables,|ERROR:|SUMMARY:|runtime error|mandoc:|CHECK failed|error:|warning:|\*\*\*|^FAIL|FAIL |Assertion|not found|No such file|undefined reference|Permission denied|cannot|not ok' "$log" | head -40
        grep -n -A4 'Violated property' "$log" | head -12
        echo '--- last lines ---'
        tail -n 12 "$log"
    } > "$log.sum"
    msg=$(tr -d '\r' < "$log.sum" | sed -e 's/%/%25/g' | awk '{ printf "%s%%0A", $0 }' | cut -c1-8000)
    echo "::error title=${label} failed (exit ${rc})::${msg}"
fi
rm -f "$log" "$log.sum" "$log.rc"
exit "$rc"
