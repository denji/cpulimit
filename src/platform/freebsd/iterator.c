/**
 *
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

/* Process iterator for FreeBSD, over libkvm (kvm_getprocs; the descriptor is opened on /dev/null,
 * so it only uses sysctl and needs no group membership). As on Linux, macOS and OpenBSD the
 * processes are copied into a snapshot when the iterator is initialised and platform/snapshot.c
 * selects the target and its descendants.
 *
 * Not run on FreeBSD as of this writing beyond the unit tests of the `vm` workflow: the fields
 * used are the documented ki_* members of struct kinfo_proc. */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>

#include "platform/snapshot.h"

#ifndef SZOMB
#define SZOMB 5 /* <sys/proc.h> */
#endif

/* One process as seen by the snapshot taken in init_process_iterator(). */
struct freebsd_rec {
    struct snap_rec s;     /* first; starttime is in microseconds since the epoch */
    struct kinfo_proc *kp; /* owned by libkvm: valid until kvm_close(), used to read argv */
    char comm[COMMLEN + 1];
};

/* 0 when the entry is a live process cpulimit may consider; -1 for zombies and kernel processes. */
static int to_rec(struct kinfo_proc *kp, struct freebsd_rec *r) {
    if (kp->ki_stat == SZOMB || (kp->ki_flag & P_SYSTEM)) {
        return -1;
    }
    r->s.pid = kp->ki_pid;
    r->s.ppid = kp->ki_ppid;
    r->s.starttime = (int64_t)kp->ki_start.tv_sec * 1000000 + (int64_t)kp->ki_start.tv_usec;
    r->s.cputime = (int64_t)(kp->ki_runtime / 1000); /* ki_runtime is in microseconds */
    r->s.selected = 0;
    r->kp = kp;
    strncpy(r->comm, kp->ki_comm, sizeof(r->comm) - 1);
    r->comm[sizeof(r->comm) - 1] = '\0';
    return 0;
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
    char errbuf[_POSIX2_LINE_MAX];
    struct kinfo_proc *procs;
    int count = 0, i;
    size_t n = 0;
    struct freebsd_rec *recs;
    int single = filter->pid != 0 && !filter->include_children;

    it->filter = filter;
    it->kd = NULL;
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;
    if ((it->kd = kvm_openfiles(NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf)) == NULL) {
        fprintf(stderr, "kvm_open: %s\n", errbuf);
        return -1;
    }
    procs = kvm_getprocs(it->kd, single ? KERN_PROC_PID : KERN_PROC_PROC, single ? filter->pid : 0, &count);
    if (procs == NULL) {
        fprintf(stderr, "kvm_getprocs: %s\n", kvm_geterr(it->kd));
        kvm_close(it->kd);
        it->kd = NULL;
        return -1;
    }
    if (count <= 0) {
        return 0; /* the target is gone: an empty answer, not an error */
    }
    recs = (struct freebsd_rec *)malloc((size_t)count * sizeof(*recs));
    if (recs == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(errno));
        kvm_close(it->kd);
        it->kd = NULL;
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (to_rec(&procs[i], &recs[n]) == 0) {
            n++;
        }
    }
    it->recs = recs;
    it->nrecs = n;
    if (snapshot_select(recs, n, sizeof(*recs), filter->pid, filter->include_children) != 0) {
        fprintf(stderr, "malloc: %s\n", strerror(errno));
        free(recs);
        it->recs = NULL;
        it->nrecs = 0;
        kvm_close(it->kd);
        it->kd = NULL;
        return -1;
    }
    return 0;
}

int get_next_process(struct process_iterator *it, struct process *p) {
    while (it->pos < it->nrecs) {
        const struct freebsd_rec *r = &it->recs[it->pos++];
        if (!r->s.selected) {
            continue;
        }
        p->pid = r->s.pid;
        p->ppid = r->s.ppid;
        p->starttime = r->s.starttime;
        p->cputime = r->s.cputime;
        p->cpu_usage = -1;
        p->group_seq = 0;
        p->command[0] = '\0';
        if (it->filter->need_command) {
            /* argv[0] as on Linux; the process name when argv cannot be read (not ours, empty) */
            char **args = kvm_getargv(it->kd, r->kp, 0);
            const char *src = (args != NULL && args[0] != NULL) ? args[0] : r->comm;
            strncpy(p->command, src, sizeof(p->command) - 1);
            p->command[sizeof(p->command) - 1] = '\0';
        }
        return 0;
    }
    return -1;
}

int close_process_iterator(struct process_iterator *it) {
    int ret = 0;
    free(it->recs);
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;
    if (it->kd != NULL && kvm_close(it->kd) == -1) {
        fprintf(stderr, "kvm_close: %s\n", strerror(errno));
        ret = -1;
    }
    it->kd = NULL;
    it->filter = NULL;
    return ret;
}
