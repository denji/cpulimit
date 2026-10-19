#!/bin/sh
# Command-line behaviour tests. Usage: BUILD=build sh test/cli_test.sh
# None of the cases below can touch an existing process: every invalid case is
# rejected before a target is looked up, and valid cases only limit a command
# that this script starts itself.

BIN=${BUILD:-build}/cpulimit
failed=0

if command -v timeout >/dev/null 2>&1; then
    TMO="timeout 30"
else
    TMO=""
fi

# expect <exit status> <message substring or -> <description> <args...>
expect() {
    want=$1
    msg=$2
    desc=$3
    shift 3
    out=$($TMO "$BIN" "$@" 2>&1 </dev/null)
    rc=$?
    ok=1
    [ "$rc" -eq "$want" ] || ok=0
    if [ "$msg" != "-" ]; then
        case $out in
            *"$msg"*) ;;
            *) ok=0 ;;
        esac
    fi
    if [ "$ok" -eq 1 ]; then
        printf 'ok    %s\n' "$desc"
    else
        printf 'FAIL  %s: exit status %d (wanted %d), wanted message "%s"\n' "$desc" "$rc" "$want" "$msg"
        printf '%s\n' "$out" | sed 's/^/        /'
        failed=$((failed + 1))
    fi
}

expect 1 "You must specify a cpu limit percentage" "no arguments"
expect 1 "limit must be in the range" "-l not a number"            -l abc true
expect 1 "limit must be in the range" "-l negative"                -l -5 true
expect 1 "limit must be in the range" "-l absurdly large"          -l 99999999 true
expect 1 "limit must be in the range" "-l with trailing garbage"   -l 50x true
expect 1 "chunk size must be in the range 1-1000000" "-c zero"           -l 50 -c 0 true
expect 1 "chunk size must be in the range 1-1000000" "-c too large"      -l 50 -c 1000001 true
expect 1 "chunk size must be in the range 1-1000000" "-c not a number"   -l 50 -c abc true
expect 1 "minimum-limited-cpu" "-m not a number"                   -l 50 -m abc true
expect 1 "minimum-limited-cpu" "-m negative"                       -l 50 -m -1 true
expect 1 "Usage:" "-m without its argument (was a segfault)"       -l 50 -m
expect 1 "Usage:" "--minimum-limited-cpu without argument"         -l 50 --minimum-limited-cpu
expect 1 "Invalid value for argument PID" "-p not a number"        -l 50 -p abc
expect 1 "Invalid value for argument PID" "-p 1"                   -l 50 -p 1
expect 1 "exactly one target process" "pid together with a command" -l 50 -p 2 true
expect 0 - "--version"                                             --version

expect 0 - "run a command"                                         -l 100 true
expect 3 - "exit status of the command is propagated"              -l 100 sh -c 'exit 3'
expect 0 - "-m accepts a number"                                   -l 100 -m 0.5 true
expect 0 - "--minimum-limited-cpu=VALUE"                           -l 100 --minimum-limited-cpu=0.5 true
expect 0 - "-c at its documented maximum"                          -l 100 -c 1000000 true

# verbose output written before fork() must not be duplicated when stdout is a pipe
out=$($TMO "$BIN" -v -l 100 true 2>&1 </dev/null | grep -c 'Running command')
if [ "$out" = "1" ]; then
    printf 'ok    %s\n' "verbose banner printed exactly once through a pipe"
else
    printf 'FAIL  %s: printed %s times\n' "verbose banner printed exactly once through a pipe" "$out"
    failed=$((failed + 1))
fi

[ "$failed" -eq 0 ]
