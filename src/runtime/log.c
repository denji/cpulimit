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

#include <stdarg.h>
#include <string.h>

#include "runtime/log.h"

static int use_syslog = 0;
static int use_console = 1;
/* openlog() keeps the pointer it is given, so the identity needs storage that lives on */
static char ident_buf[64];

void log_set_syslog(int enable, const char *ident) {
    if (enable) {
        strncpy(ident_buf, ident != NULL ? ident : "cpulimit", sizeof(ident_buf) - 1);
        ident_buf[sizeof(ident_buf) - 1] = '\0';
        openlog(ident_buf, LOG_PID, LOG_DAEMON);
    } else if (use_syslog) {
        closelog();
    }
    use_syslog = enable != 0;
}

void log_set_console(int enable) {
    use_console = enable != 0;
}

void report(int prio, FILE *stream, const char *fmt, ...) {
    va_list ap;
    if (use_console) {
        va_start(ap, fmt);
        vfprintf(stream, fmt, ap);
        va_end(ap);
    }
    if (use_syslog) {
        va_start(ap, fmt);
        /* syslog adds its own line ending: strip the one the console text carries */
        {
            char line[512];
            size_t len;
            vsnprintf(line, sizeof(line), fmt, ap);
            len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0) {
                syslog(prio, "%s", line);
            }
        }
        va_end(ap);
    }
}
