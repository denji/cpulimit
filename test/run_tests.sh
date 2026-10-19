#!/bin/sh
# Test driver. Usage: BUILD=build sh test/run_tests.sh
# Each test is a program that exits 0 on success. A failing test does not stop
# the run; the script exits non-zero if any test failed.

BUILD=${BUILD:-build}
failed=0
total=0

run() {
    name=$1
    shift
    total=$((total + 1))
    printf 'RUN   %s\n' "$name"
    if "$@"; then
        printf 'PASS  %s\n' "$name"
    else
        rc=$?
        printf 'FAIL  %s (exit status %d)\n' "$name" "$rc"
        failed=$((failed + 1))
    fi
}

dir=$(dirname "$0")

run process_iterator_test "$BUILD/test/process_iterator_test"
run util_test "$BUILD/test/util_test"
run limiter_test "$BUILD/test/limiter_test"
run procfs_parse_test "$BUILD/test/procfs_parse_test"
run group_test "$BUILD/test/group_test"
run cli_parse_test "$BUILD/test/cli_parse_test"
run lock_test "$BUILD/test/lock_test"
run cli_test env BUILD="$BUILD" sh "$dir/cli_test.sh"
# a hung test must fail the run, not hang it
if command -v timeout >/dev/null 2>&1; then
    run integration_test timeout 600 env BUILD="$BUILD" sh "$dir/integration_test.sh"
else
    run integration_test env BUILD="$BUILD" sh "$dir/integration_test.sh"
fi

printf '\n%d test(s), %d failed\n' "$total" "$failed"
[ "$failed" -eq 0 ]
