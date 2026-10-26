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

#ifndef CPULIMIT_DAEMON_H
#define CPULIMIT_DAEMON_H

/*
 * Detach from the controlling terminal: fork, setsid, fork again (so the daemon can never
 * re-acquire a terminal), chdir("/"), umask(0), and point stdin/stdout/stderr at /dev/null.
 *
 * The pid file is written by the FINAL process, after the second fork, and the original
 * parent only exits once it has been written, so a script that runs `cpulimit -d -P f ...`
 * can read f as soon as the command returns. A pid file that already exists and names a
 * live process is refused.
 *
 * Returns 0 in the daemon, -1 on error (errno set, nothing has been detached then, the
 * caller still owns the terminal and can report the error). Never returns in the
 * intermediate processes: they _exit().
 *
 * Must be called before any thread is created and before the target is resolved.
 */
int daemonize(const char *pidfile);

#endif
