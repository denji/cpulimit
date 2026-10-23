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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime/lock.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/*
 * Make sure dir is a directory that only we can write to. It may already exist (and
 * then it must be ours: another user must not be able to substitute a lock file), so
 * a pre-existing directory owned by someone else, or writable by others, is refused.
 */
static int ensure_private_dir(const char *dir) {
    struct stat st;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return -1;
    }
    /* lstat: a symbolic link planted in place of the directory must not be followed */
    if (lstat(dir, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

static int resolve_rundir(const char *rundir, char *out, size_t size) {
    const char *xdg;
    int n;
    if (rundir != NULL) {
        n = snprintf(out, size, "%s", rundir);
        return (n > 0 && (size_t)n < size) ? 0 : -1;
    }
    xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg != NULL && xdg[0] == '/') {
        char base[PATH_MAX];
        struct stat st;
        /* the base must already exist and be ours: it is created by the login system */
        if (stat(xdg, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid()) {
            n = snprintf(base, sizeof(base), "%s/cpulimit", xdg);
            if (n > 0 && (size_t)n < sizeof(base) && (size_t)n < size) {
                memcpy(out, base, (size_t)n + 1);
                return 0;
            }
        }
    }
    n = snprintf(out, size, "/tmp/cpulimit-%lu", (unsigned long)geteuid());
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

enum lock_result target_lock_acquire(pid_t pid, int64_t starttime, const char *rundir, int *fd_out, pid_t *holder) {
    char dir[PATH_MAX];
    char path[PATH_MAX + 64];
    struct flock fl;
    int fd, n;
    if (resolve_rundir(rundir, dir, sizeof(dir)) != 0) {
        errno = ENAMETOOLONG;
        return LOCK_ERROR;
    }
    if (ensure_private_dir(dir) != 0) {
        return LOCK_ERROR;
    }
    n = snprintf(path, sizeof(path), "%s/%ld-%lld.lock", dir, (long)pid, (long long)starttime);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return LOCK_ERROR;
    }
    /* O_NOFOLLOW: never write through a symbolic link, even inside our own directory */
    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return LOCK_ERROR;
    }
#if !O_CLOEXEC
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    if (fcntl(fd, F_SETLK, &fl) == 0) {
        *fd_out = fd;
        return LOCK_ACQUIRED;
    }
    if (errno == EACCES || errno == EAGAIN) {
        if (holder != NULL) {
            *holder = 0;
            if (fcntl(fd, F_GETLK, &fl) == 0 && fl.l_type != F_UNLCK) {
                *holder = fl.l_pid;
            }
        }
        close(fd);
        return LOCK_BUSY;
    }
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return LOCK_ERROR;
}

void target_lock_release(int fd) {
    if (fd >= 0) {
        close(fd);  /* closing the descriptor releases the record lock */
    }
}
