/**
 * cpulimit - a CPU limiter for Linux
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

#ifndef CPULIMIT_LOCK_H
#define CPULIMIT_LOCK_H

#include <stdint.h>
#include <sys/types.h>

enum lock_result {
    LOCK_ACQUIRED = 0,
    LOCK_BUSY,       /* another cpulimit holds the lock for this target */
    LOCK_ERROR       /* the lock could not be set up (errno set); the caller decides whether to go on */
};

/*
 * Take the exclusive "I am limiting this target" lock. A target is a (pid, start
 * time) pair, so a recycled pid is a different target.
 *
 * The lock is an fcntl record lock on a file in a private directory (see
 * docs/ARCHITECTURE.md section 7). It lives as long as the process holds the
 * descriptor and is released by the kernel when the process dies, so a crashed
 * limiter never leaves a stale lock.
 *
 * rundir: directory to use, or NULL for the default
 *         ($XDG_RUNTIME_DIR/cpulimit, else /tmp/cpulimit-<uid>).
 * On success *fd_out is the descriptor to keep open (close it to release).
 * On LOCK_BUSY, *holder (when not NULL) is the pid of the process holding the lock.
 */
enum lock_result target_lock_acquire(pid_t pid, int64_t starttime, const char *rundir, int *fd_out, pid_t *holder);

void target_lock_release(int fd);

#endif
