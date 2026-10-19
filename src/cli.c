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

#include <float.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>

#include "cli.h"
#include "util.h"

#define VERSION "0.3.2"

/* long-only options (no short form) */
enum { OPT_NO_LOCK = 256, OPT_NO_BOOST };

const char *cli_version(void) {
    return VERSION;
}

void cli_usage(FILE *stream, const char *program_name, int ncpu) {
    fprintf(stream, "Usage: %s [OPTIONS...] TARGET\n", program_name);
    fprintf(stream, "   OPTIONS\n");
    fprintf(stream, "      -l, --limit=N                percentage of cpu allowed from 0 to %d (required)\n", 100*ncpu);
    fprintf(stream, "      -c, --chunk-size=N           enforce CPU limit in N microsecond chunks\n");
    fprintf(stream, "      -v, --verbose                show control statistics\n");
    fprintf(stream, "      -V, --version                show program version number\n");
    fprintf(stream, "      -z, --lazy                   exit if there is no target process, or if it dies\n");
    fprintf(stream, "      -i, --include-children       limit also the children processes\n");
    fprintf(stream, "      -m, --minimum-limited-cpu=M  processes using less than M of a CPU (0.05 = 5%%) are left running\n");
    fprintf(stream, "      -d, --daemon                 detach from the terminal and run in the background\n");
    fprintf(stream, "      -s, --syslog                 also log events to syslog\n");
    fprintf(stream, "      -P, --pidfile=FILE           write the daemon's pid to FILE (requires --daemon)\n");
    fprintf(stream, "          --no-lock                do not refuse a target that another cpulimit is limiting\n");
    fprintf(stream, "          --no-boost               do not try to raise cpulimit's own priority\n");
    fprintf(stream, "      -h, --help                   display this help and exit\n");
    fprintf(stream, "   TARGET must be exactly one of these:\n");
    fprintf(stream, "      -p, --pid=N            pid of the process (implies -z)\n");
    fprintf(stream, "      -e, --exe=FILE         name of the executable program file or path name\n");
    fprintf(stream, "      COMMAND [ARGS]         run this command and limit it (implies -z)\n");
}

/* a malformed number becomes -1, which the range checks below reject with the usual message */
static long number_or_minus_one(const char *s) {
    long v;
    return parse_long(s, 0, INT_MAX, &v) == 0 ? v : -1;
}

enum cli_result cli_parse(int argc, char **argv, int ncpu, long pid_max, struct cli_config *cfg, FILE *err) {
    /* A string listing valid short options letters ('+': stop at the first non-option, so the
     * command's own options are never interpreted) */
    static const char short_options[] = "+p:e:l:c:vVzim:hdsP:";
    /* An array describing valid long options */
    static const struct option long_options[] = {
        { "pid",        required_argument,     NULL, 'p' },
        { "exe",        required_argument,     NULL, 'e' },
        { "limit",      required_argument,     NULL, 'l' },
        { "chunk-size", required_argument,     NULL, 'c' },
        { "verbose",    no_argument,           NULL, 'v' },
        { "version",    no_argument,           NULL, 'V' },
        { "lazy",       no_argument,           NULL, 'z' },
        { "include-children", no_argument,     NULL, 'i' },
        { "minimum-limited-cpu", required_argument, NULL, 'm' },
        { "daemon",     no_argument,           NULL, 'd' },
        { "syslog",     no_argument,           NULL, 's' },
        { "pidfile",    required_argument,     NULL, 'P' },
        { "no-lock",    no_argument,           NULL, OPT_NO_LOCK },
        { "no-boost",   no_argument,           NULL, OPT_NO_BOOST },
        { "help",       no_argument,           NULL, 'h' },
        { 0,            0,                     0,     0  }
    };
    int limit_ok = 0;
    int next_option;
    int option_index = 0;
    int target_count;
    double d;

    memset(cfg, 0, sizeof(*cfg));
    cfg->chunk_us = CL_DEFAULT_CHUNK_US;
    if (ncpu < 1) {
        ncpu = 1;  /* the count is unknown: assume one, a limit above it is meaningless */
    }

    /* getopt keeps its position in globals: reset so the function can be called again */
    optind = 0;
    /* getopt writes its own messages ("invalid option -- 'x'") to the real stderr, exactly as
     * the program always did; for any other stream (tests, fuzzing) we write the message */
    opterr = (err == stderr);

    while ((next_option = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (next_option) {
        case 'p':
            cfg->pid = (int)number_or_minus_one(optarg);
            cfg->pid_set = 1;
            break;
        case 'e':
            cfg->exe = optarg;
            cfg->exe_set = 1;
            break;
        case 'l':
            cfg->limit_pct = (int)number_or_minus_one(optarg);
            limit_ok = 1;
            break;
        case 'c':
            cfg->chunk_us = (int)number_or_minus_one(optarg);
            break;
        case 'v':
            cfg->verbose = 1;
            break;
        case 'V':
            return CLI_VERSION;
        case 'z':
            cfg->lazy = 1;
            break;
        case 'i':
            cfg->include_children = 1;
            break;
        case 'm':
            if (parse_double(optarg, 0, DBL_MAX, &d) != 0) {
                fprintf(err, "Error: minimum-limited-cpu must be a non-negative number\n");
                return CLI_USAGE;
            }
            cfg->min_cpu = d;
            break;
        case 'd':
            cfg->daemonize = 1;
            break;
        case 's':
            cfg->use_syslog = 1;
            break;
        case 'P':
            cfg->pidfile = optarg;
            break;
        case OPT_NO_LOCK:
            cfg->no_lock = 1;
            break;
        case OPT_NO_BOOST:
            cfg->no_boost = 1;
            break;
        case 'h':
            return CLI_HELP;
        case '?':
        default:
            /* unknown option or missing argument; getopt already said which when opterr is set */
            if (!opterr) {
                fprintf(err, "%s: invalid option or missing argument\n", argc > 0 ? argv[0] : "cpulimit");
            }
            return CLI_USAGE;
        }
    }

    if (cfg->pid_set && !pid_in_range(cfg->pid, pid_max)) {
        fprintf(err, "Error: Invalid value for argument PID\n");
        return CLI_USAGE;
    }
    if (cfg->pid_set) {
        cfg->lazy = 1;
    }

    if (!limit_ok) {
        fprintf(err, "Error: You must specify a cpu limit percentage\n");
        return CLI_USAGE;
    }
    if (cfg->limit_pct < 0 || cfg->limit_pct > 100 * ncpu) {
        fprintf(err, "Error: limit must be in the range 0-%d00\n", ncpu);
        return CLI_USAGE;
    }

    if (cfg->chunk_us < 1 || cfg->chunk_us > CL_MAX_CHUNK_US) {
        fprintf(err, "Error: chunk size must be in the range 1-%d\n", CL_MAX_CHUNK_US);
        return CLI_USAGE;
    }

    if (cfg->exe_set && cfg->exe[0] == '\0') {
        fprintf(err, "Error: the executable name must not be empty\n");
        return CLI_USAGE;
    }

    if (optind < argc) {
        cfg->command_start = optind;
        cfg->lazy = 1;  /* "COMMAND [ARGS]: run this command and limit it (implies -z)" */
    }
    target_count = cfg->exe_set + cfg->pid_set + (cfg->command_start != 0);
    if (target_count == 0) {
        fprintf(err, "Error: You must specify one target process, either by name, pid, or command line\n");
        return CLI_USAGE;
    }
    if (target_count > 1) {
        fprintf(err, "Error: You must specify exactly one target process, either by name, pid, or command line\n");
        return CLI_USAGE;
    }

    if (cfg->daemonize && cfg->command_start != 0) {
        fprintf(err, "Error: --daemon cannot run a command (its output would be lost); use -p or -e\n");
        return CLI_USAGE;
    }
    if (cfg->pidfile != NULL && !cfg->daemonize) {
        fprintf(err, "Error: --pidfile requires --daemon\n");
        return CLI_USAGE;
    }
    if (cfg->pidfile != NULL && cfg->pidfile[0] == '\0') {
        fprintf(err, "Error: the pid file name must not be empty\n");
        return CLI_USAGE;
    }
    return CLI_OK;
}
