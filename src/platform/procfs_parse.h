/**
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012, by:  Angelo Marletta <angelo dot marletta at gmail dot com>
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

#ifndef CPULIMIT_PROCFS_PARSE_H
#define CPULIMIT_PROCFS_PARSE_H

#include <stdint.h>
#include <sys/types.h>

/* The fields of /proc/<pid>/stat that cpulimit uses. Times are in clock ticks. */
struct proc_stat {
    char state;          /* field 3: R, S, D, Z, T, ... */
    pid_t ppid;          /* field 4 */
    uint64_t utime;      /* field 14 */
    uint64_t stime;      /* field 15 */
    uint64_t starttime;  /* field 22: ticks since boot, constant for the life of a process */
};

/*
 * Parse the text of /proc/<pid>/stat (a NUL-terminated string, a trailing
 * newline is fine).
 *
 * The command name (field 2) is arbitrary text and may itself contain spaces
 * and parentheses, so the numeric fields are located after the LAST ')'.
 * Numbers must be plain decimal digits and must fit their type; the parser
 * never calls strto*() or sscanf(), so no input can trigger the undefined
 * behavior those have on overflow. No allocation, no globals.
 *
 * Return 0 on success. Return -1 if the text is truncated or malformed;
 * *out is then left untouched.
 */
int parse_proc_stat(const char *buf, struct proc_stat *out);

/*
 * ticks * 1000 / clk_tck without intermediate overflow. Saturates at INT64_MAX
 * instead of wrapping; returns 0 when clk_tck <= 0.
 */
int64_t ticks_to_ms(uint64_t ticks, long clk_tck);

#endif
