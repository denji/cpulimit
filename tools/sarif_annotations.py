#!/usr/bin/env python3
# Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn the SARIF files written by a code scanning run into one GitHub annotation.

    python3 tools/sarif_annotations.py DIR [LABEL]

The code scanning alerts page needs a login; annotations can be read through the checks API
without one, so this prints every finding (rule, location, message) as a single notice and the
same list in the job log. Exit status is always 0: the alerts themselves are reported by code
scanning, this only makes them readable.
"""
import glob
import json
import os
import sys

MAX_CHARS = 8000


def esc(s):
    return s.replace('%', '%25').replace('\r', '').replace('\n', '%0A')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    label = sys.argv[2] if len(sys.argv) > 2 else 'code scanning'
    rows = []
    for path in sorted(glob.glob(os.path.join(root, '**', '*.sarif'), recursive=True)):
        with open(path) as f:
            data = json.load(f)
        for run in data.get('runs', []):
            rules = {r.get('id'): r for r in run.get('tool', {}).get('driver', {}).get('rules', [])}
            for res in run.get('results', []):
                rid = res.get('ruleId', '?')
                level = res.get('level') or rules.get(rid, {}).get('defaultConfiguration', {}).get('level', 'warning')
                locs = res.get('locations') or [{}]
                phys = locs[0].get('physicalLocation', {})
                uri = phys.get('artifactLocation', {}).get('uri', '?')
                line = phys.get('region', {}).get('startLine', 0)
                msg = res.get('message', {}).get('text', '').replace('\n', ' ')
                sev = rules.get(rid, {}).get('properties', {}).get('security-severity', '')
                rows.append((level, sev, rid, uri, line, msg))
    rows.sort(key=lambda r: (r[0] != 'error', r[2], r[3], r[4]))
    lines = ['%d finding(s)' % len(rows)]
    for level, sev, rid, uri, line, msg in rows:
        lines.append('%s%s %s %s:%s %s' % (level, ' sev=' + sev if sev else '', rid, uri, line, msg[:200]))
    text = '\n'.join(lines)
    print(text)
    print('::notice title=%s (%d)::%s' % (label, len(rows), esc(text)[:MAX_CHARS]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
