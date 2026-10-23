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

#ifndef CPULIMIT_LOG_H
#define CPULIMIT_LOG_H

#include <stdio.h>
#include <syslog.h>

#ifdef __GNUC__
#define CL_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define CL_PRINTF(a, b)
#endif

/*
 * Output policy. By default messages go to the given stream, byte for byte what
 * the program has always printed (scripts may parse them). Optionally they are
 * also sent to syslog, and in a daemon the streams are silenced.
 *
 *   log_set_syslog(1, ident)  also log to syslog(3) under ident (LOG_DAEMON)
 *   log_set_console(0)        stop writing to stdout/stderr (used when detached)
 */
void log_set_syslog(int enable, const char *ident);
void log_set_console(int enable);

/* write to stream (when the console is enabled) and to syslog (when enabled) with priority prio */
void report(int prio, FILE *stream, const char *fmt, ...) CL_PRINTF(3, 4);

#endif
