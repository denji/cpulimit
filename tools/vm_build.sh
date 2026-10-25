#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Build and run the unit tests inside a BSD or illumos VM (vmactions/*-vm), keeping the whole
# transcript in a file instead of the job log:
#     PREP="pkg_add gmake" sh tools/vm_build.sh NAME
# writes vm-NAME.log and vm-NAME.rc (and vm-NAME.probe on OpenBSD) in the current directory and always exits 0. The VM actions
# copy the working directory back to the runner, where a normal step turns the log into an error
# annotation (tools/ci_run.sh), which can be read without logging in. Only POSIX sh is used:
# the VMs have no bash.
name=$1
log="vm-$name.log"
{
    uname -a
    if [ -n "${PREP:-}" ]; then
        echo "+ $PREP"
        eval "$PREP" || echo "prepare failed: $?"
    fi
    MAKE=gmake
    command -v gmake >/dev/null 2>&1 || MAKE=make
    echo "+ $MAKE: $(command -v $MAKE)"
    $MAKE --version 2>&1 | head -n 1
    # FULL=1 also runs the integration tests (they need /proc, so only on Linux guests)
    if [ -n "${FULL:-}" ]; then
        $MAKE WERROR=1 CFLAGS="-O2 -g" all && $MAKE CFLAGS="-O2 -g" check
    else
        $MAKE WERROR=1 CFLAGS="-O2 -g" all && $MAKE CFLAGS="-O2 -g" check SKIP_INTEGRATION=1
    fi
} >"$log" 2>&1
echo $? >"vm-$name.rc"
# raw kernel fields for the OpenBSD iterator (prints nothing on other systems)
if [ "$(uname -s)" = OpenBSD ]; then
    { cc -o iterprobe test/support/iterprobe.c && ./iterprobe; } >"vm-$name.probe" 2>&1
fi
exit 0
