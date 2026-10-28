/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* snapshot_select() against a plain fixed-point model: random process tables (forests, cycles,
 * a pid that is its own parent, duplicate pids, a missing target) are selected both ways and
 * compared. Records are wider than snap_rec, as in the platform iterators, so the stride is
 * tested too. Built with the fault-injecting allocator: allocation failure and leaks are checked. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/fault_alloc.h"
#include "platform/snapshot.h"

#define MAXN 48

struct wide {
    struct snap_rec s;
    char pad[13]; /* an odd size: stride is not a multiple of sizeof(snap_rec) */
};

static uint32_t rng = 987654321;
TEST_PRNG static uint32_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* the answer by repeating "a child of a selected process is selected" until nothing changes */
static void model(const struct wide *r, size_t n, pid_t target, int children, int *out) {
    size_t i, k;
    int changed = 1;
    for (i = 0; i < n; i++) {
        out[i] = (target == 0 || r[i].s.pid == target);
    }
    if (target == 0 || !children) {
        return;
    }
    while (changed) {
        changed = 0;
        for (i = 0; i < n; i++) {
            for (k = 0; k < n && !out[i]; k++) {
                if (out[k] && r[k].s.pid == r[i].s.ppid) {
                    out[i] = 1;
                    changed = 1;
                }
            }
        }
    }
}

static void fill(struct wide *r, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        memset(&r[i], 0xA5, sizeof(r[i]));
        r[i].s.pid = (pid_t)(1 + next() % 40); /* duplicates happen */
        r[i].s.ppid = (pid_t)(next() % 41);    /* 0 and own pid happen */
        r[i].s.selected = (int)(next() % 3);   /* stale values must be overwritten */
    }
}

static void test_random_tables(void) {
    int round;
    for (round = 0; round < 20000; round++) {
        struct wide r[MAXN];
        int want[MAXN];
        size_t n = next() % (MAXN + 1), i;
        pid_t target = (pid_t)(next() % 44); /* 0 = all; above 40 = no such process */
        int children = (int)(next() & 1);
        fill(r, n);
        model(r, n, target, children, want);
        fault_reset();
        CHECK(snapshot_select(r, n, sizeof(r[0]), target, children) == 0);
        for (i = 0; i < n; i++) {
            CHECK((r[i].s.selected != 0) == (want[i] != 0));
            CHECK(r[i].s.selected == 0 || r[i].s.selected == 1);
        }
        CHECK(fault_outstanding() == 0);
    }
}

static void test_chain_and_siblings(void) {
    struct wide r[6];
    size_t i;
    pid_t pid[6] = { 10, 11, 12, 13, 20, 21 };
    pid_t ppid[6] = { 1, 10, 11, 12, 1, 20 }; /* 10 > 11 > 12 > 13 ; 20 > 21 */
    memset(r, 0, sizeof(r));
    for (i = 0; i < 6; i++) {
        r[i].s.pid = pid[i];
        r[i].s.ppid = ppid[i];
    }
    CHECK(snapshot_select(r, 6, sizeof(r[0]), 11, 1) == 0);
    CHECK(!r[0].s.selected && r[1].s.selected && r[2].s.selected && r[3].s.selected);
    CHECK(!r[4].s.selected && !r[5].s.selected);
    CHECK(snapshot_select(r, 6, sizeof(r[0]), 11, 0) == 0);
    CHECK(!r[0].s.selected && r[1].s.selected && !r[2].s.selected && !r[3].s.selected);
    CHECK(snapshot_select(r, 6, sizeof(r[0]), 0, 0) == 0);
    for (i = 0; i < 6; i++) {
        CHECK(r[i].s.selected == 1);
    }
    CHECK(snapshot_select(r, 0, sizeof(r[0]), 11, 1) == 0); /* empty table */
}

static void test_cycle_terminates(void) {
    struct wide r[3];
    memset(r, 0, sizeof(r));
    r[0].s.pid = 5;
    r[0].s.ppid = 6;
    r[1].s.pid = 6;
    r[1].s.ppid = 5; /* 5 and 6 are each other's parent */
    r[2].s.pid = 7;
    r[2].s.ppid = 7; /* its own parent */
    CHECK(snapshot_select(r, 3, sizeof(r[0]), 5, 1) == 0);
    CHECK(r[0].s.selected && r[1].s.selected && !r[2].s.selected);
    CHECK(snapshot_select(r, 3, sizeof(r[0]), 7, 1) == 0);
    CHECK(!r[0].s.selected && !r[1].s.selected && r[2].s.selected);
}

static void test_allocation_failure(void) {
    struct wide r[4];
    long k, total;
    size_t i;
    memset(r, 0, sizeof(r));
    for (i = 0; i < 4; i++) {
        r[i].s.pid = (pid_t)(10 + i);
        r[i].s.ppid = (pid_t)(9 + i);
    }
    fault_reset();
    CHECK(snapshot_select(r, 4, sizeof(r[0]), 10, 1) == 0);
    total = fault_total();
    CHECK(total >= 2);
    /* every allocation made can fail: the call reports it, selects nothing, leaks nothing */
    for (k = 0; k < total; k++) {
        fault_reset();
        fault_fail_after(k);
        CHECK(snapshot_select(r, 4, sizeof(r[0]), 10, 1) == -1);
        for (i = 0; i < 4; i++) {
            CHECK(r[i].s.selected == 0);
        }
        CHECK(fault_outstanding() == 0);
    }
    /* no allocation is made when the answer needs none */
    fault_reset();
    fault_fail_after(0);
    CHECK(snapshot_select(r, 4, sizeof(r[0]), 10, 0) == 0);
    CHECK(snapshot_select(r, 4, sizeof(r[0]), 0, 1) == 0);
}

int main(void) {
    test_chain_and_siblings();
    test_cycle_terminates();
    test_random_tables();
    test_allocation_failure();
    puts("snapshot_test: ok");
    return 0;
}
