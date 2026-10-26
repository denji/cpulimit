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

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "util/util.h"

#define NS_PER_SEC 1000000000LL

int parse_long(const char *s, long min, long max, long *out) {
    char *end = NULL;
    long v;
    if (s == NULL || *s == '\0' || isspace((unsigned char)*s)) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE) {
        return -1;
    }
    if (*end != '\0') {
        errno = EINVAL;
        return -1;
    }
    if (v < min || v > max) {
        errno = ERANGE;
        return -1;
    }
    *out = v;
    return 0;
}

int parse_double(const char *s, double min, double max, double *out) {
    char *end = NULL;
    double v;
    if (s == NULL || *s == '\0' || isspace((unsigned char)*s)) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    v = strtod(s, &end);
    if (errno == ERANGE) {
        return -1;
    }
    if (*end != '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!isfinite(v)) {
        errno = EINVAL;
        return -1;
    }
    if (v < min || v > max) {
        errno = ERANGE;
        return -1;
    }
    *out = v;
    return 0;
}

const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

int64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

struct timespec ns_to_timespec(int64_t ns) {
    struct timespec ts;
    if (ns < 0) {
        ns = 0;
    }
    ts.tv_sec = (time_t)(ns / NS_PER_SEC);
    ts.tv_nsec = (long)(ns % NS_PER_SEC);
    return ts;
}

int sleep_ns(int64_t ns) {
    struct timespec ts;
    if (ns <= 0) {
        return 0;
    }
    ts = ns_to_timespec(ns);
    return nanosleep(&ts, NULL) == 0 ? 0 : -1;
}

int wait_ns(int64_t ns, const sigset_t *unblocked) {
    struct timespec ts;
    if (ns <= 0) {
        return 0;
    }
    if (unblocked == NULL) {
        return sleep_ns(ns);
    }
    ts = ns_to_timespec(ns);
    /* no descriptors: this is only a sleep whose signal mask is switched atomically */
    return pselect(0, NULL, NULL, NULL, &ts, unblocked) == 0 ? 0 : -1;
}

int pid_in_range(long pid, long pid_max) {
    if (pid <= 1) {
        return 0;
    }
    return pid_max <= 0 || pid < pid_max;
}
