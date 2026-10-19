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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* the pid file this daemon created, removed when it exits normally */
static char owned_pidfile[4096];

/* remove the pid file, but only if it still names this process (another instance may have replaced it) */
static void remove_owned_pidfile(void) {
    char buf[32];
    ssize_t n;
    int fd;
    if (owned_pidfile[0] == '\0') {
        return;
    }
    fd = open(owned_pidfile, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n] = '\0';
        if (strtol(buf, NULL, 10) == (long)getpid()) {
            (void)unlink(owned_pidfile);
        }
    }
}

/* is the pid file left by a limiter that is still alive? */
static int pidfile_is_live(const char *pidfile) {
    char buf[32];
    ssize_t n;
    long pid;
    char *end;
    int fd = open(pidfile, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    pid = strtol(buf, &end, 10);
    if (end == buf || pid <= 1) {
        return 0;
    }
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

static int write_pidfile(const char *pidfile) {
    char buf[32];
    int len, fd;
    ssize_t written;
    /* O_EXCL: never write through a link or over a file somebody else owns; a stale file
     * (its process is gone) is removed first */
    if (pidfile_is_live(pidfile)) {
        errno = EEXIST;
        return -1;
    }
    (void)unlink(pidfile);
    fd = open(pidfile, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }
    len = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
    written = write(fd, buf, (size_t)len);
    if (close(fd) != 0 || written != len) {
        (void)unlink(pidfile);
        errno = EIO;
        return -1;
    }
    if (strlen(pidfile) < sizeof(owned_pidfile)) {
        strcpy(owned_pidfile, pidfile);
        atexit(remove_owned_pidfile);
    }
    return 0;
}

/* tell the original parent how startup went (one byte); if the pipe is gone there is no one to tell */
static void send_status(int fd, unsigned char status) {
    ssize_t n;
    do {
        n = write(fd, &status, 1);
    } while (n < 0 && errno == EINTR);
}

/* read exactly one status byte from the daemon: 0 = running, anything else = failed */
static int wait_for_status(int fd) {
    unsigned char status = 1;
    ssize_t n;
    do {
        n = read(fd, &status, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1 ? status : 1;
}

int daemonize(const char *pidfile) {
    int pipefd[2];
    pid_t pid;
    int devnull;
    /* The pidfile path is resolved before chdir("/"), so a relative name means what the user meant */
    char absolute[4096];
    if (pidfile != NULL && pidfile[0] != '/') {
        char cwd[3072];
        int n;
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return -1;
        }
        n = snprintf(absolute, sizeof(absolute), "%s/%s", cwd, pidfile);
        if (n <= 0 || (size_t)n >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        pidfile = absolute;
    }
    if (pidfile != NULL && pidfile_is_live(pidfile)) {
        errno = EEXIST;  /* refuse before detaching, while the user can still see the error */
        return -1;
    }
    if (pipe(pipefd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = saved;
        return -1;
    }
    if (pid > 0) {
        /* original parent: wait for the daemon to report that it is up, then leave */
        int status;
        close(pipefd[1]);
        status = wait_for_status(pipefd[0]);
        close(pipefd[0]);
        (void)waitpid(pid, NULL, 0);
        _exit(status == 0 ? 0 : 1);
    }
    /* first child */
    close(pipefd[0]);
    if (setsid() < 0) {
        unsigned char fail = 1;
        send_status(pipefd[1], fail);
        _exit(1);
    }
    pid = fork();
    if (pid < 0) {
        unsigned char fail = 1;
        send_status(pipefd[1], fail);
        _exit(1);
    }
    if (pid > 0) {
        close(pipefd[1]);
        _exit(0);  /* the session leader leaves: the daemon can never get a controlling terminal */
    }
    /* the daemon */
    {
        unsigned char ok = 0;
        unsigned char fail = 1;
        umask(0);
        if (chdir("/") != 0) {
            send_status(pipefd[1], fail);
            _exit(1);
        }
        if (pidfile != NULL && write_pidfile(pidfile) != 0) {
            send_status(pipefd[1], fail);
            _exit(1);
        }
        devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) {
            send_status(pipefd[1], fail);
            _exit(1);
        }
        (void)dup2(devnull, STDIN_FILENO);
        (void)dup2(devnull, STDOUT_FILENO);
        (void)dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) {
            close(devnull);
        }
        send_status(pipefd[1], ok);
        close(pipefd[1]);
    }
    return 0;
}
