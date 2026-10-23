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
    # a hung test must fail the run instead of holding it (and the CI job) for hours
    if command -v timeout >/dev/null 2>&1; then
        timeout "${TEST_TIMEOUT:-900}" "$@"
    else
        "$@"
    fi
    rc=$?
    if [ "$rc" -eq 0 ]; then
        printf 'PASS  %s\n' "$name"
    else
        printf 'FAIL  %s (exit status %d%s)\n' "$name" "$rc" "$([ "$rc" -eq 124 ] && echo ', timed out')"
        failed=$((failed + 1))
    fi
}

dir=$(dirname "$0")

run process_iterator_test "$BUILD/test/process_iterator_test"
run util_test "$BUILD/test/util_test"
run limiter_test "$BUILD/test/limiter_test"
run procfs_parse_test "$BUILD/test/procfs_parse_test"
run group_test "$BUILD/test/group_test"
run list_test "$BUILD/test/list_test"
run cli_parse_test "$BUILD/test/cli_parse_test"
run lock_test "$BUILD/test/lock_test"
run cli_test env BUILD="$BUILD" sh "$dir/integration/cli_test.sh"
run version_test env BUILD="$BUILD" sh "$dir/integration/version_test.sh"
if [ -n "${SKIP_INTEGRATION:-}" ]; then
    printf 'SKIP  integration_test (SKIP_INTEGRATION set)\n'
else
    run integration_test env BUILD="$BUILD" sh "$dir/integration/integration_test.sh"
fi

printf '\n%d test(s), %d failed\n' "$total" "$failed"
[ "$failed" -eq 0 ]
