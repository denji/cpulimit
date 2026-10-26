/**
 * cpulimit - limit the CPU usage of a process
 *
 * Copyright (C) 2005-2012 Angelo Marletta
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

#ifndef CPULIMIT_UTIL_H
#define CPULIMIT_UTIL_H

#include <signal.h>
#include <stdint.h>
#include <time.h>

/*
 * Strict numeric parsers. The whole string must be a number: no leading
 * whitespace, no trailing characters, no overflow, value inside [min, max].
 * parse_double() additionally rejects NaN and infinities.
 * Return 0 and store the value in *out on success; return -1 and leave *out
 * untouched otherwise (errno is EINVAL for malformed input, ERANGE for a value
 * outside the representable or requested range).
 */
int parse_long(const char *s, long min, long max, long *out);
int parse_double(const char *s, double min, double max, double *out);

/*
 * The part of path after its last '/' (the whole string if there is none).
 * Never modifies its argument, unlike POSIX basename().
 */
const char *base_name(const char *path);

/* Monotonic clock in nanoseconds (immune to wall-clock steps). */
int64_t now_ns(void);

/*
 * Split a duration into a valid struct timespec (0 <= tv_nsec < 10^9).
 * Negative durations are treated as zero.
 */
struct timespec ns_to_timespec(int64_t ns);

/*
 * Sleep for ns nanoseconds. Return 0 when the full time elapsed (or ns <= 0),
 * -1 when interrupted by a signal, so the caller can check its stop flag.
 */
int sleep_ns(int64_t ns);

/*
 * Like sleep_ns(), but the signal mask is replaced by *unblocked for the
 * duration of the wait, atomically (pselect). A caller that keeps its stop
 * signals blocked and passes the mask WITHOUT them here cannot lose a signal
 * that arrives between its flag check and the sleep: it is delivered inside the
 * wait and ends it with -1. With unblocked == NULL this is sleep_ns().
 */
int wait_ns(int64_t ns, const sigset_t *unblocked);

/*
 * Is pid a valid target pid? pid_max is the kernel limit (pids are below it) or
 * <= 0 when it could not be determined, in which case only the lower bound is
 * checked: an unreadable limit must not make every pid invalid.
 */
int pid_in_range(long pid, long pid_max);

#endif
