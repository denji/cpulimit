#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Describe an unfamiliar system from inside its VM (vmactions/*-vm), to decide whether a process
# iterator for it is feasible:   sh tools/vm_probe.sh NAME
# writes vm-NAME.log and vm-NAME.rc (always 0) in the current directory; tools/vm_notice.sh
# turns the log into an annotation on the runner. POSIX sh only.
name=$1
{
    uname -a
    echo "--- /proc"
    ls /proc 2>&1 | head -n 20
    echo "--- /proc/self/stat"
    cat /proc/self/stat 2>&1
    echo "--- /proc/self/status"
    head -n 12 /proc/self/status 2>&1
    echo "--- CLK_TCK"
    getconf CLK_TCK 2>&1
    echo "--- tools"
    for t in cc gcc clang make gmake python3; do
        command -v "$t" || echo "no $t"
    done
    echo "--- package managers"
    for t in apt-get pkgman pkg_add pkg zypper; do
        command -v "$t" || echo "no $t"
    done
} >"vm-$name.log" 2>&1
echo 0 >"vm-$name.rc"
exit 0
