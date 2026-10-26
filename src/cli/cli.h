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

#ifndef CPULIMIT_CLI_H
#define CPULIMIT_CLI_H

#include <stdio.h>

#define CL_DEFAULT_CHUNK_US 100000
#define CL_MAX_CHUNK_US 1000000

struct cli_config {
    /* target: exactly one of these is set */
    int pid; /* -p */
    int pid_set;
    const char *exe; /* -e */
    int exe_set;
    int command_start; /* index of the first word of the command in argv, or 0 */
    /* limits */
    int limit_pct;        /* -l, percent of one CPU */
    int chunk_us;         /* -c */
    double min_cpu;       /* -m, fraction of a CPU below which a process is left running */
    int include_children; /* -i */
    int lazy;             /* -z (implied by -p) */
    /* operation */
    int verbose;         /* -v */
    int daemonize;       /* -d */
    int use_syslog;      /* -s */
    const char *pidfile; /* -P */
    int no_lock;         /* --no-lock */
    int no_boost;        /* --no-boost */
};

enum cli_result {
    CLI_OK = 0,
    CLI_USAGE,  /* an error message was written to err; the caller prints usage to stderr, exit 1 */
    CLI_HELP,   /* -h: the caller prints usage to stdout, exit 1 (as always) */
    CLI_VERSION /* -V: the caller prints the version, exit 0 */
};

/*
 * Parse and fully validate the command line. Pure: never exits, writes only to err
 * (the "Error: ..." line, plus getopt's own message for unknown options), touches no
 * global state except getopt's (reset on entry, so it can be called repeatedly, e.g.
 * by a fuzzer).
 *
 *   ncpu     number of CPUs (>= 1) for the upper bound of -l
 *   pid_max  kernel pid limit, or <= 0 when unknown (see pid_in_range())
 *
 * *cfg is only meaningful when CLI_OK is returned.
 */
enum cli_result cli_parse(int argc, char **argv, int ncpu, long pid_max, struct cli_config *cfg, FILE *err);

void cli_usage(FILE *stream, const char *program_name, int ncpu);

const char *cli_version(void);

#endif
