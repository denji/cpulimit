/**
 *
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
 *
 * Author: Simon Sigurdhsson
 *
 */

#include <errno.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include "process_iterator.h"

/* One process as seen by the snapshot taken in init_process_iterator(). */
struct apple_rec {
    pid_t pid;
    pid_t ppid;
    int64_t starttime;  /* microseconds since the epoch: constant for the life of a process */
    int64_t cputime;    /* milliseconds */
    int selected;       /* part of the answer for this filter */
};

/* proc_pidinfo() reports CPU time in mach absolute time units: nanoseconds on Intel, but not on
 * Apple silicon, so convert with the timebase. */
static int64_t mach_to_ms(uint64_t ticks) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
    }
    if (tb.denom == 0) {
        return 0;
    }
    /* ticks * numer / denom / 1e6 without overflowing 64 bits for any realistic uptime */
    return (int64_t)((double)ticks * (double)tb.numer / (double)tb.denom / 1e6);
}

/* 0 on success, -1 when the process is gone, not ours to inspect, or a zombie. */
static int read_rec(pid_t pid, struct apple_rec *r) {
    struct proc_taskallinfo ti;
    int bytes = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &ti, (int)sizeof(ti));
    if (bytes <= 0 || (size_t)bytes < sizeof(ti)) {
        return -1;  /* ESRCH or EPERM: skip it quietly, as the Linux iterator does */
    }
    if (ti.pbsd.pbi_status == SZOMB) {
        return -1;
    }
    r->pid = (pid_t)ti.pbsd.pbi_pid;
    r->ppid = (pid_t)ti.pbsd.pbi_ppid;
    r->starttime = (int64_t)ti.pbsd.pbi_start_tvsec * 1000000 + (int64_t)ti.pbsd.pbi_start_tvusec;
    r->cputime = mach_to_ms(ti.ptinfo.pti_total_user) + mach_to_ms(ti.ptinfo.pti_total_system);
    r->selected = 0;
    return 0;
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
    it->filter = filter;
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;

    if (filter->pid != 0 && !filter->include_children) {
        struct apple_rec *one = (struct apple_rec *)malloc(sizeof(*one));
        if (one == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(errno));
            return -1;
        }
        if (read_rec(filter->pid, one) == 0) {
            one->selected = 1;
            it->recs = one;
            it->nrecs = 1;
        } else {
            free(one);
        }
        return 0;
    }

    {
        /* proc_listpids() returns a size in BYTES. Leave room for processes started meanwhile. */
        int bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
        size_t cap, n = 0, i, k;
        pid_t *pids;
        struct apple_rec *recs;
        if (bytes <= 0) {
            fprintf(stderr, "proc_listpids: %s\n", strerror(errno));
            return -1;
        }
        cap = (size_t)bytes / sizeof(pid_t) + 64;
        pids = (pid_t *)malloc(cap * sizeof(pid_t));
        recs = (struct apple_rec *)malloc(cap * sizeof(*recs));
        if (pids == NULL || recs == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(errno));
            free(pids);
            free(recs);
            return -1;
        }
        bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, (int)(cap * sizeof(pid_t)));
        if (bytes <= 0) {
            fprintf(stderr, "proc_listpids: %s\n", strerror(errno));
            free(pids);
            free(recs);
            return -1;
        }
        for (i = 0; i < (size_t)bytes / sizeof(pid_t) && n < cap; i++) {
            if (pids[i] != 0 && read_rec(pids[i], &recs[n]) == 0 && recs[n].pid == pids[i]) {
                n++;
            }
        }
        free(pids);

        if (filter->pid == 0) {
            for (i = 0; i < n; i++) {
                recs[i].selected = 1;
            }
        } else {
            /* the target and every descendant: repeat until no new child is found (a tree is at
             * most n deep, so n passes always suffice; a pass is cheap next to proc_pidinfo) */
            int changed = 1;
            for (i = 0; i < n; i++) {
                recs[i].selected = (recs[i].pid == filter->pid);
            }
            while (changed) {
                changed = 0;
                for (i = 0; i < n; i++) {
                    if (recs[i].selected) {
                        continue;
                    }
                    for (k = 0; k < n; k++) {
                        if (recs[k].selected && recs[k].pid == recs[i].ppid && recs[i].pid != recs[i].ppid) {
                            recs[i].selected = 1;
                            changed = 1;
                            break;
                        }
                    }
                }
            }
        }
        it->recs = recs;
        it->nrecs = n;
    }
    return 0;
}

int get_next_process(struct process_iterator *it, struct process *p) {
    while (it->pos < it->nrecs) {
        const struct apple_rec *r = &it->recs[it->pos++];
        if (!r->selected) {
            continue;
        }
        p->pid = r->pid;
        p->ppid = r->ppid;
        p->starttime = r->starttime;
        p->cputime = r->cputime;
        p->cpu_usage = -1;
        p->group_seq = 0;
        p->command[0] = '\0';
        if (it->filter->need_command) {
            char path[PROC_PIDPATHINFO_MAXSIZE];
            int len = proc_pidpath(r->pid, path, sizeof(path));
            if (len > 0 && (size_t)len < sizeof(p->command)) {
                memcpy(p->command, path, (size_t)len);
                p->command[len] = '\0';
            }
        }
        return 0;
    }
    return -1;
}

int close_process_iterator(struct process_iterator *it) {
    free(it->recs);
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;
    it->filter = NULL;
    return 0;
}
