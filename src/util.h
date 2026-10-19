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

#ifndef CPULIMIT_UTIL_H
#define CPULIMIT_UTIL_H

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

#endif
