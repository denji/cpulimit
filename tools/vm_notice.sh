#!/bin/sh
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
# Print a file copied back from a VM as a GitHub notice annotation (readable through the checks
# API without logging in) and in the job log:   sh tools/vm_notice.sh FILE TITLE
file=$1
title=$2
if [ ! -f "$file" ]; then
    echo "::warning title=${title}::${file} was not produced (the VM did not run the script)"
    exit 0
fi
cat "$file"
msg=$(tr -d '\r' <"$file" | sed -e 's/%/%25/g' | awk '{ printf "%s%%0A", $0 }' | cut -c1-8000)
echo "::notice title=${title}::${msg}"
