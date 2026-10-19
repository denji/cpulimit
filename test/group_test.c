/* process_group.c against a scripted iterator and an allocator that can fail. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "fake_iterator.h"
#include "fault_alloc.h"
#include "process_group.h"

#define MS 1000000LL
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static struct process *listed(struct process_group *pg, pid_t pid) {
    struct list_node *node;
    for (node = pg->proclist->first; node != NULL; node = node->next) {
        struct process *p = (struct process *)node->data;
        if (p->pid == pid) {
            return p;
        }
    }
    return NULL;
}

static int stored_count(struct process_group *pg) {
    int i, n = 0;
    for (i = 0; i < PIDHASH_SZ; i++) {
        if (pg->proctable[i] != NULL) {
            n += pg->proctable[i]->count;
        }
    }
    return n;
}

static void test_cpu_usage_arithmetic(void) {
    struct process_group pg;
    struct fake_proc s1[] = { { 100, 1, 5, 0 } };
    struct fake_proc s2[] = { { 100, 1, 5, 25 } };
    struct fake_proc s3[] = { { 100, 1, 5, 75 } };
    struct process *p;
    int64_t t;
    fault_reset();
    fake_set_snapshot(s1, 1);
    CHECK(init_process_group(&pg, 100, 0) == 0);
    CHECK(pg.proclist->count == 1);
    p = listed(&pg, 100);
    CHECK(p != NULL && p->cpu_usage == -1);  /* not measured yet */
    t = pg.last_update_ns;

    /* 25 ms of CPU in 50 ms: the first sample is taken as is */
    fake_set_snapshot(s2, 1);
    CHECK(update_process_group_at(&pg, t + 50 * MS) == 0);
    p = listed(&pg, 100);
    CHECK(p != NULL && fabs(p->cpu_usage - 0.5) < 1e-12);

    /* 50 ms of CPU in the next 50 ms, smoothed with ALFA = 0.08 */
    fake_set_snapshot(s3, 1);
    CHECK(update_process_group_at(&pg, t + 100 * MS) == 0);
    p = listed(&pg, 100);
    CHECK(p != NULL && fabs(p->cpu_usage - (0.92 * 0.5 + 0.08 * 1.0)) < 1e-12);

    /* a sample closer than 20 ms to the previous one is ignored */
    {
        double before = p->cpu_usage;
        int64_t last = pg.last_update_ns;
        CHECK(update_process_group_at(&pg, t + 110 * MS) == 0);
        p = listed(&pg, 100);
        CHECK(p != NULL && p->cpu_usage == before);
        CHECK(pg.last_update_ns == last);
    }
    CHECK(fake_open_iterators() == 0);
    CHECK(close_process_group(&pg) == 0);
    CHECK(fault_outstanding() == 0);
}

/* The old code compared only the pid and then aborted on assert(starttime ==). */
static void test_pid_reuse(void) {
    struct process_group pg;
    struct fake_proc s1[] = { { 100, 1, 5, 0 } };
    struct fake_proc s2[] = { { 100, 1, 5, 25 } };
    struct fake_proc s3[] = { { 100, 1, 99, 3 } };   /* same pid, different start time */
    struct fake_proc s4[] = { { 100, 1, 99, 53 } };
    struct process *p;
    int64_t t;
    fault_reset();
    fake_set_snapshot(s1, 1);
    CHECK(init_process_group(&pg, 0, 0) == 0);
    t = pg.last_update_ns;
    fake_set_snapshot(s2, 1);
    CHECK(update_process_group_at(&pg, t + 50 * MS) == 0);
    CHECK(listed(&pg, 100) != NULL && listed(&pg, 100)->cpu_usage > 0);

    fake_set_snapshot(s3, 1);
    CHECK(update_process_group_at(&pg, t + 100 * MS) == 0);
    p = listed(&pg, 100);
    CHECK(pg.proclist->count == 1 && stored_count(&pg) == 1);
    CHECK(p != NULL && p->starttime == 99 && p->cputime == 3);
    CHECK(p->cpu_usage == -1);  /* a new process, nothing inherited from the dead one */

    fake_set_snapshot(s4, 1);
    CHECK(update_process_group_at(&pg, t + 150 * MS) == 0);
    p = listed(&pg, 100);
    CHECK(p != NULL && fabs(p->cpu_usage - 1.0) < 1e-12);  /* 50 ms in 50 ms */
    CHECK(close_process_group(&pg) == 0);
    CHECK(fault_outstanding() == 0);
}

/* Terminated processes were never forgotten: the table only grew. */
static void test_terminated_processes_are_purged(void) {
    struct process_group pg;
    struct fake_proc both[] = { { 100, 1, 5, 0 }, { 101, 1, 6, 0 } };
    struct fake_proc one[] = { { 100, 1, 5, 10 } };
    int64_t t;
    int i;
    fault_reset();
    fake_set_snapshot(both, 2);
    CHECK(init_process_group(&pg, 0, 0) == 0);
    CHECK(stored_count(&pg) == 2);
    t = pg.last_update_ns;
    fake_set_snapshot(one, 1);
    CHECK(update_process_group_at(&pg, t + 50 * MS) == 0);
    CHECK(pg.proclist->count == 1);
    CHECK(stored_count(&pg) == 1);
    CHECK(remove_process(&pg, 101) == 2);  /* already purged */

    /* churn: 2000 short-lived processes, one per cycle, next to a long-lived one */
    for (i = 0; i < 2000; i++) {
        struct fake_proc cycle[2];
        cycle[0].pid = 100;
        cycle[0].ppid = 1;
        cycle[0].starttime = 5;
        cycle[0].cputime = 10 + i;
        cycle[1].pid = 1000 + i;
        cycle[1].ppid = 1;
        cycle[1].starttime = 7;
        cycle[1].cputime = 0;
        fake_set_snapshot(cycle, 2);
        CHECK(update_process_group_at(&pg, t + (100 + 30 * (int64_t)i) * MS) == 0);
        CHECK(stored_count(&pg) == 2);
    }
    CHECK(close_process_group(&pg) == 0);
    CHECK(fault_outstanding() == 0);
}

static void test_remove_process(void) {
    struct process_group pg;
    struct fake_proc two[] = { { 100, 1, 5, 0 }, { 101, 1, 6, 0 } };
    fault_reset();
    fake_set_snapshot(two, 2);
    CHECK(init_process_group(&pg, 0, 0) == 0);
    CHECK(remove_process(&pg, 100) == 0);
    CHECK(listed(&pg, 100) == NULL);         /* also dropped from the list: no dangling pointer */
    CHECK(pg.proclist->count == 1);
    CHECK(stored_count(&pg) == 1);
    CHECK(remove_process(&pg, 100) == 2);    /* unknown now */
    CHECK(remove_process(&pg, 4000) != 0);   /* never existed */
    CHECK(remove_process(&pg, -1) != 0);     /* a negative pid must not crash the hash */
    CHECK(close_process_group(&pg) == 0);
    CHECK(fault_outstanding() == 0);
}

static void test_empty_and_wrong_pid(void) {
    struct process_group pg;
    fault_reset();
    fake_set_snapshot(NULL, 0);
    CHECK(init_process_group(&pg, -1, 0) == 0);
    CHECK(pg.proclist->count == 0);
    CHECK(update_process_group(&pg) == 0);
    CHECK(pg.proclist->count == 0);
    CHECK(close_process_group(&pg) == 0);
    CHECK(close_process_group(&pg) == 0);  /* safe to close twice */
    CHECK(fault_outstanding() == 0);
}

/* The whole life of a group, run with the allocator failing from the k-th call on. */
static int scenario(void) {
    static struct fake_proc a[] = { { 100, 1, 5, 0 }, { 101, 1, 6, 0 }, { 5000, 1, 7, 0 } };
    static struct fake_proc b[] = { { 100, 1, 5, 40 }, { 5000, 1, 7, 10 }, { 6000, 1, 8, 0 } };
    static struct fake_proc c[] = { { 100, 1, 77, 5 }, { 6000, 1, 8, 20 } };
    struct process_group pg;
    int failures = 0;
    int64_t t;
    fake_set_snapshot(a, ARRAY_LEN(a));
    if (init_process_group(&pg, 0, 0) != 0) {
        return 1;  /* init closes the group itself when it fails */
    }
    t = pg.last_update_ns;
    fake_set_snapshot(b, ARRAY_LEN(b));
    failures += update_process_group_at(&pg, t + 50 * MS) != 0;
    fake_set_snapshot(c, ARRAY_LEN(c));
    failures += update_process_group_at(&pg, t + 100 * MS) != 0;
    fake_set_snapshot(a, ARRAY_LEN(a));
    failures += update_process_group_at(&pg, t + 150 * MS) != 0;
    (void)remove_process(&pg, 100);
    close_process_group(&pg);
    return failures;
}

static void test_allocation_failures(void) {
    long k;
    int saw_failure = 0, saw_success = 0;
    long total;
    fault_reset();
    CHECK(scenario() == 0);
    total = fault_total();
    CHECK(total > 10);
    CHECK(fault_outstanding() == 0);
    for (k = 0; k <= total + 2; k++) {
        int failures;
        fault_reset();
        fault_fail_after(k);
        failures = scenario();
        CHECK(fault_outstanding() == 0);  /* no leak whichever allocation failed */
        CHECK(fake_open_iterators() == 0);
        if (failures != 0) {
            saw_failure = 1;
        } else {
            saw_success = 1;
        }
    }
    CHECK(saw_failure && saw_success);   /* the injection really reached the code under test */
}

int main(void) {
    test_cpu_usage_arithmetic();
    test_pid_reuse();
    test_terminated_processes_are_purged();
    test_remove_process();
    test_empty_and_wrong_pid();
    test_allocation_failures();
    return 0;
}
