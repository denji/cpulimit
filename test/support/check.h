/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal assertion macro for the unit tests. Unlike assert() it does not
 * depend on NDEBUG, so a test can never silently turn into a no-op. */
#ifndef CPULIMIT_TEST_CHECK_H
#define CPULIMIT_TEST_CHECK_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

/* A failed CHECK calls exit(), which must never leave a busy or sleeping child behind: an orphan
 * keeps the test's output pipe open, so the CI step (or `make check | tee`) waits for it forever.
 * Two layers: the kernel kills a child when its parent dies (Linux), and everything registered with
 * test_track_child() is killed from an atexit handler (other systems, process groups included). */
#ifdef __linux__
#define TEST_DIE_WITH_PARENT() ((void)prctl(PR_SET_PDEATHSIG, SIGKILL))
#else
#define TEST_DIE_WITH_PARENT() ((void)0)
#endif

#define TEST_MAX_KIDS 64
static pid_t test_kids[TEST_MAX_KIDS];
static int test_nkids;
static pid_t test_owner;

static inline void test_kill_kids(void) {
    int i;
    if (getpid() != test_owner) {
        return;  /* a forked child that calls exit() must not kill its siblings */
    }
    for (i = 0; i < test_nkids; i++) {
        kill(-test_kids[i], SIGKILL);  /* the whole group, when the child leads one */
        kill(test_kids[i], SIGKILL);
    }
}

static inline void test_track_child(pid_t pid) {
    if (test_nkids == 0) {
        test_owner = getpid();
        atexit(test_kill_kids);
    }
    if (test_nkids < TEST_MAX_KIDS) {
        test_kids[test_nkids++] = pid;
    }
}

/* Test PRNGs wrap around on purpose; the integer sanitizers must not flag that (src/ is not exempt). */
#if defined(__clang__)
#define TEST_PRNG __attribute__((no_sanitize("unsigned-integer-overflow", "unsigned-shift-base")))
#else
#define TEST_PRNG
#endif

#endif
