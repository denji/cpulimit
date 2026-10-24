/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* A scripted stand-in for the platform process iterator, so process_group.c can be
 * tested deterministically: pid reuse, vanished processes, exact CPU arithmetic.
 * Link it INSTEAD of process_iterator.o. */
#ifndef FAKE_ITERATOR_H
#define FAKE_ITERATOR_H

#include <stdint.h>
#include <sys/types.h>

struct fake_proc {
    pid_t pid;
    pid_t ppid;
    int64_t starttime;
    int64_t cputime;  /* milliseconds */
};

/* what the next iteration (init_process_iterator ... close_process_iterator) will yield */
void fake_set_snapshot(const struct fake_proc *procs, int count);
/* iterators opened and not closed (must be 0 after every update) */
int fake_open_iterators(void);

#endif
