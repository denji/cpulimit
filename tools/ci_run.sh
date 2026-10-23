#!/bin/bash
# Run a command; when it fails, repeat the interesting part of its output as a GitHub error
# annotation:   bash tools/ci_run.sh LABEL command args...
# Annotations can be read through the checks API without logging in, which the raw job log
# cannot, so a failure can be diagnosed without opening the Actions UI. Full output is still
# printed to the job log.
set -o pipefail
label=$1; shift
log=$(mktemp)
"$@" 2>&1 | tee "$log"
rc=$?
if [ "$rc" -ne 0 ]; then
    # error and FAIL lines first (what broke), then the last lines (where it stopped)
    {
        grep -nE ': FAILURE|Runtime|Generated [0-9]+ VCC|Solving with|SAT checker|variables,|ERROR:|SUMMARY:|runtime error|mandoc:|CHECK failed|error:|warning:|\*\*\*|^FAIL|FAIL |Assertion|not found|No such file|undefined reference|Permission denied|cannot|not ok' "$log" | head -40
        grep -n -A4 'Violated property' "$log" | head -12
        echo '--- last lines ---'
        tail -n 12 "$log"
    } > "$log.sum"
    msg=$(sed -e 's/%/%25/g' -e 's/\r//g' "$log.sum" | awk '{ printf "%s%%0A", $0 }' | cut -c1-8000)
    echo "::error title=${label} failed (exit ${rc})::${msg}"
fi
rm -f "$log" "$log.sum"
exit "$rc"
