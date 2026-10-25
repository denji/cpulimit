/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* Dump the raw kern.proc fields the OpenBSD iterator relies on, for a busy child over time and
 * for a zombie. Run by tools/vm_build.sh inside the OpenBSD VM; the output is the evidence for
 * how p_rtime, the tick counters and the zombie state really behave. Prints nothing elsewhere. */
#ifdef __OpenBSD__
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int get(pid_t pid, struct kinfo_proc *kp) {
    int mib[6] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid, (int)sizeof(*kp), 1 };
    size_t len = sizeof(*kp);
    memset(kp, 0, sizeof(*kp));
    return (sysctl(mib, 6, kp, &len, NULL, 0) == 0 && len == sizeof(*kp)) ? 0 : -1;
}

static void show(const char *tag, int i, const struct kinfo_proc *kp) {
    printf("probe %s %2d stat=%d psflags=0x%x rtime=%u.%06u uticks=%llu sticks=%llu iticks=%llu\n", tag, i,
           (int)kp->p_stat, (unsigned)kp->p_psflags, (unsigned)kp->p_rtime_sec, (unsigned)kp->p_rtime_usec,
           (unsigned long long)kp->p_uticks, (unsigned long long)kp->p_sticks, (unsigned long long)kp->p_iticks);
}

static void nap(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(void) {
    struct kinfo_proc kp;
    struct clockinfo ci;
    int cmib[2] = { CTL_KERN, KERN_CLOCKRATE };
    size_t cl = sizeof(ci);
    pid_t busy, zomb;
    int i;
    if (sysctl(cmib, 2, &ci, &cl, NULL, 0) == 0) {
        printf("probe clockrate hz=%d stathz=%d tick=%d\n", ci.hz, ci.stathz, ci.tick);
    }
    printf("probe sizeof(kinfo_proc)=%zu\n", sizeof(kp));
    busy = fork();
    if (busy == 0) {
        for (;;) {
        }
    }
    for (i = 0; i < 24; i++) {
        nap(50);
        if (get(busy, &kp) == 0) {
            show("busy", i, &kp);
        } else {
            printf("probe busy %2d unreadable\n", i);
        }
    }
    kill(busy, SIGKILL);
    waitpid(busy, NULL, 0);
    zomb = fork();
    if (zomb == 0) {
        _exit(0);
    }
    nap(300);
    if (get(zomb, &kp) == 0) {
        show("zombie", 0, &kp);
    } else {
        printf("probe zombie unreadable (not listed by KERN_PROC_PID)\n");
    }
    waitpid(zomb, NULL, 0);
    return 0;
}
#else
int main(void) {
    return 0;
}
#endif
