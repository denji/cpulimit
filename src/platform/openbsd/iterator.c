/**
 *
 * cpulimit - a CPU limiter for Linux
 *
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

/* OpenBSD process iterator.
 *
 * Reads kern.proc.* through sysctl(2), so it needs neither libkvm nor any group membership. The
 * processes are copied into a snapshot when the iterator is initialised, as on macOS and Linux;
 * the same snapshot logic selects the target and its descendants.
 *
 * Facts this file relies on, from the OpenBSD sources (sys/kern/kern_sysctl.c, sys/sys/sysctl.h)
 * and from a probe run in CI (test/support/iterprobe.c, OpenBSD 8.0, hz = stathz = 100):
 *  - KERN_PROC_ALL returns every process except kernel threads, zombies included.
 *  - A zombie has p_stat == SDEAD and its tick counters are 0.
 *  - p_rtime is only brought up to date when a process is switched out: for a busy process it
 *    advanced in 200 ms steps. p_uticks, p_sticks and p_iticks (statistics clock hits) advance
 *    continuously, about 6 per 50 ms, so they are the CPU time source.
 *  - KERN_PROC_ARGV returns cnt + 1 pointers (the last NULL) followed by the strings.
 * The unit tests (process table, name matching through KERN_PROC_ARGV, zombies, descendants) and the
 * command-mode tests pass on OpenBSD 7.9 in CI; on 8.0 the only failure was the timing bound of one
 * test (see test/unit/process_iterator_test.c). */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include "platform/iterator.h"

#ifndef ARG_MAX
#define ARG_MAX 524288
#endif

/* Values from <sys/proc.h>, which userland may not see; used only when the header does not
 * define them. */
#ifndef SZOMB
#define SZOMB 5
#endif
#ifndef SDEAD
#define SDEAD 6
#endif

/* One process as seen by the snapshot taken in init_process_iterator(). */
struct bsd_rec {
    pid_t pid;
    pid_t ppid;
    int64_t starttime; /* microseconds since the epoch: constant for the life of a process */
    int64_t cputime;   /* milliseconds */
    int selected;      /* part of the answer for this filter */
    char comm[KI_MAXCOMLEN];
};

/* Returns a malloc'ed array of kinfo_proc and its length, or NULL on error (errno set). The size
 * is asked for first and padded, because processes may start between the two calls; ENOMEM means
 * the table grew anyway, so ask again. */
static struct kinfo_proc *fetch_procs(int op, int arg, size_t *count) {
    int attempt;
    for (attempt = 0; attempt < 8; attempt++) {
        int mib[6] = { CTL_KERN, KERN_PROC, op, arg, (int)sizeof(struct kinfo_proc), 0 };
        size_t len = 0;
        struct kinfo_proc *buf;
        if (sysctl(mib, 6, NULL, &len, NULL, 0) == -1) {
            return NULL;
        }
        len += len / 8 + 16 * sizeof(struct kinfo_proc);
        buf = (struct kinfo_proc *)malloc(len);
        if (buf == NULL) {
            return NULL;
        }
        mib[5] = (int)(len / sizeof(struct kinfo_proc));
        if (sysctl(mib, 6, buf, &len, NULL, 0) == 0) {
            *count = len / sizeof(struct kinfo_proc);
            return buf;
        }
        free(buf);
        if (errno != ENOMEM) {
            return NULL;
        }
    }
    errno = ENOMEM;
    return NULL;
}

/* Statistics clock rate: p_uticks, p_sticks and p_iticks count its ticks. */
static int64_t stat_hz(void) {
    static int64_t hz;
    if (hz == 0) {
        int mib[2] = { CTL_KERN, KERN_CLOCKRATE };
        struct clockinfo ci;
        size_t len = sizeof(ci);
        if (sysctl(mib, 2, &ci, &len, NULL, 0) == 0 && len == sizeof(ci)) {
            hz = ci.stathz > 0 ? ci.stathz : ci.hz;
        }
        if (hz <= 0) {
            hz = 100;
        }
    }
    return hz;
}

/* 0 when the entry is a live process that cpulimit may consider: not a zombie, not a kernel
 * thread. */
static int to_rec(const struct kinfo_proc *kp, struct bsd_rec *r) {
    if (kp->p_stat == SZOMB || kp->p_stat == SDEAD) {
        return -1;
    }
#ifdef PS_ZOMBIE
    if (kp->p_psflags & PS_ZOMBIE) {
        return -1;
    }
#endif
#ifdef PS_SYSTEM
    if (kp->p_psflags & PS_SYSTEM) {
        return -1;
    }
#endif
    r->pid = (pid_t)kp->p_pid;
    r->ppid = (pid_t)kp->p_ppid;
    r->starttime = (int64_t)kp->p_ustart_sec * 1000000 + (int64_t)kp->p_ustart_usec;
    /* CPU time from the statistics ticks: the kernel adds them to a running process at every
     * statistics clock tick. p_rtime is only added to when the process is switched out, so a
     * process that is on a CPU for a whole 100 ms scheduling quantum would be seen using nothing
     * and then twice its share. */
    r->cputime = (int64_t)(kp->p_uticks + kp->p_sticks + kp->p_iticks) * 1000 / stat_hz();
    r->selected = 0;
    memcpy(r->comm, kp->p_comm, sizeof(r->comm));
    r->comm[sizeof(r->comm) - 1] = '\0';
    return 0;
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
    struct kinfo_proc *kps;
    size_t nkp = 0, n = 0, i, k;
    struct bsd_rec *recs;

    it->filter = filter;
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;

    if (filter->pid != 0 && !filter->include_children) {
        kps = fetch_procs(KERN_PROC_PID, filter->pid, &nkp);
    } else {
        kps = fetch_procs(KERN_PROC_ALL, 0, &nkp);
    }
    if (kps == NULL) {
        /* a target that is gone is not an error; anything else is */
        if (filter->pid != 0 && !filter->include_children && errno == ESRCH) {
            return 0;
        }
        fprintf(stderr, "sysctl kern.proc: %s\n", strerror(errno));
        return -1;
    }
    recs = (struct bsd_rec *)malloc((nkp + 1) * sizeof(*recs));
    if (recs == NULL) {
        fprintf(stderr, "malloc: %s\n", strerror(errno));
        free(kps);
        return -1;
    }
    for (i = 0; i < nkp; i++) {
        if (to_rec(&kps[i], &recs[n]) == 0) {
            n++;
        }
    }
    free(kps);

    if (filter->pid == 0) {
        for (i = 0; i < n; i++) {
            recs[i].selected = 1;
        }
    } else if (!filter->include_children) {
        for (i = 0; i < n; i++) {
            recs[i].selected = (recs[i].pid == filter->pid);
        }
    } else {
        /* the target and every descendant: repeat until no new child is found (a tree is at most
         * n deep, so n passes always suffice) */
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
    return 0;
}

/* Copy argv[0] of pid into out. KERN_PROC_ARGV returns an array of pointers, terminated by a
 * NULL, followed by the strings. The pointer values are not trusted (they may be addresses in
 * the target or offsets); the first string is located by counting the pointers instead.
 * Returns 0 on success, -1 when it cannot be read (gone, not ours, empty). */
static int read_argv0(pid_t pid, char *out, size_t outsz) {
    size_t cap = 4096;
    while (cap <= (size_t)ARG_MAX) {
        int mib[4] = { CTL_KERN, KERN_PROC_ARGS, (int)pid, KERN_PROC_ARGV };
        size_t len = cap, nptr = 0, start;
        char *buf = (char *)malloc(cap);
        char **av;
        if (buf == NULL) {
            return -1;
        }
        if (sysctl(mib, 4, buf, &len, NULL, 0) == -1) {
            int err = errno;
            free(buf);
            if (err == ENOMEM) {
                cap *= 2;
                continue;
            }
            return -1;
        }
        av = (char **)buf;
        while ((nptr + 1) * sizeof(char *) <= len && av[nptr] != NULL) {
            nptr++;
        }
        start = (nptr + 1) * sizeof(char *);
        if (nptr == 0 || start >= len || memchr(buf + start, '\0', len - start) == NULL) {
            free(buf);
            return -1;
        }
        strncpy(out, buf + start, outsz - 1);
        out[outsz - 1] = '\0';
        free(buf);
        return 0;
    }
    return -1;
}

int get_next_process(struct process_iterator *it, struct process *p) {
    while (it->pos < it->nrecs) {
        const struct bsd_rec *r = &it->recs[it->pos++];
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
            /* argv[0] as on Linux; the process name when argv cannot be read */
            if (read_argv0(r->pid, p->command, sizeof(p->command)) != 0) {
                strncpy(p->command, r->comm, sizeof(p->command) - 1);
                p->command[sizeof(p->command) - 1] = '\0';
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
