/**
 *
 * cpulimit - limit the CPU usage of a process
 *
 * Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef CPULIMIT_SNAPSHOT_H
#define CPULIMIT_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* The part of a process record that every snapshot-based iterator (Linux, macOS, OpenBSD, NetBSD)
 * keeps. A platform record puts it FIRST and may add members after it, so an array of platform
 * records is also an array of snap_rec spaced `stride` bytes apart. */
struct snap_rec {
    pid_t pid;
    pid_t ppid;
    int64_t starttime; /* identity: only compared for equality, together with pid */
    int64_t cputime;   /* milliseconds */
    int selected;      /* part of the answer for this filter */
};

/* Set `selected` on the records a process filter asks for:
 *   target == 0                 every record
 *   !include_children           the record whose pid is target (none if it is gone)
 *   include_children            that record and all of its descendants
 * Descendants are found breadth first over an index sorted by ppid, O(n log n), and a record is
 * visited once, so a cycle (a pid that is its own parent) cannot loop. Returns 0, or -1 when the
 * index could not be allocated (nothing is selected then). */
int snapshot_select(void *recs, size_t n, size_t stride, pid_t target, int include_children);

#endif
