/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* list.c against a model: every operation is applied to the list and to a plain array, and the
 * two are compared after each step (order, count, first/last, links in both directions).
 * Built with the fault-injecting allocator, so leaks and add_elem() failure are checked too. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/fault_alloc.h"
#include "core/list.h"

#define MAXN 64

struct item {
    int key;
    int pad;
};

struct model {
    struct item *e[MAXN];
    int n;
};

static void verify(struct list *l, const struct model *m) {
    struct list_node *n;
    int i;
    CHECK(get_list_count(l) == m->n);
    CHECK(is_empty_list(l) == (m->n == 0));
    CHECK(first_elem(l) == (m->n ? (void *)m->e[0] : NULL));
    CHECK(last_elem(l) == (m->n ? (void *)m->e[m->n - 1] : NULL));
    CHECK(first_node(l) == l->first && last_node(l) == l->last);
    CHECK((m->n == 0) == (l->first == NULL && l->last == NULL));
    for (i = 0, n = l->first; i < m->n; i++, n = n->next) {
        CHECK(n != NULL && n->data == m->e[i]);
        if (i == 0) {
            CHECK(n->previous == NULL);
        } else {
            CHECK(n->previous != NULL && n->previous->next == n);
        }
    }
    CHECK(n == NULL);
    if (m->n) {
        CHECK(l->last->next == NULL && l->first->previous == NULL);
    }
}

static uint32_t rng = 12345;
TEST_PRNG static uint32_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void test_random_operations(void) {
    struct list l;
    struct model m = { { 0 }, 0 };
    int step, i;
    fault_reset();
    init_list(&l, (int)sizeof(int));
    for (step = 0; step < 20000; step++) {
        switch (next() % 6) {
        case 0:
        case 1: /* add */
            if (m.n < MAXN) {
                struct item *it = malloc(sizeof(*it));
                CHECK(it != NULL);
                it->key = (int)(next() % 20);
                it->pad = 0;
                CHECK(add_elem(&l, it) != NULL);
                m.e[m.n++] = it;
            }
            break;
        case 2: /* delete_node at random position (the caller owns the element) */
            if (m.n) {
                int p = (int)(next() % (uint32_t)m.n);
                struct list_node *n = l.first;
                struct item *it = m.e[p];
                for (i = 0; i < p; i++)
                    n = n->next;
                delete_node(&l, n);
                memmove(&m.e[p], &m.e[p + 1], (size_t)(m.n - p - 1) * sizeof(m.e[0]));
                m.n--;
                free(it);
            }
            break;
        case 3: /* locate by key: must find the FIRST element with that key */
        {
            int key = (int)(next() % 24), want = -1;
            for (i = 0; i < m.n; i++)
                if (m.e[i]->key == key) {
                    want = i;
                    break;
                }
            CHECK(locate_elem(&l, &key) == (want < 0 ? NULL : (void *)m.e[want]));
            CHECK((locate_node(&l, &key) == NULL) == (want < 0));
            if (want >= 0)
                CHECK(locate_node(&l, &key)->data == m.e[want]);
        } break;
        case 4: /* offset search on the second field */
        {
            int zero = 0;
            CHECK(xlocate_elem(&l, &zero, (int)sizeof(int), (int)sizeof(int)) == (m.n ? (void *)m.e[0] : NULL));
            zero = 7;
            CHECK(xlocate_elem(&l, &zero, (int)sizeof(int), (int)sizeof(int)) == NULL);
        } break;
        default:
            break;
        }
        verify(&l, &m);
    }
    for (i = 0; i < m.n; i++)
        free(m.e[i]);
    clear_list(&l);
    CHECK(l.count == 0 && l.first == NULL && l.last == NULL);
    CHECK(fault_outstanding() == 0);
}

static void test_destroy_variants(void) {
    struct list l;
    int i;
    struct list_node *n;
    fault_reset();
    init_list(&l, 4);
    for (i = 0; i < 5; i++) {
        int *v = fault_malloc(sizeof(*v));
        *v = i;
        CHECK(add_elem(&l, v) != NULL);
    }
    CHECK(fault_outstanding() == 10);
    n = l.first->next; /* destroy_node frees node AND element */
    destroy_node(&l, n);
    CHECK(get_list_count(&l) == 4 && fault_outstanding() == 8);
    destroy_node(&l, l.first);
    destroy_node(&l, l.last);
    CHECK(get_list_count(&l) == 2 && fault_outstanding() == 4);
    destroy_list(&l); /* and so does destroy_list */
    CHECK(fault_outstanding() == 0 && is_empty_list(&l));
    destroy_list(&l); /* idempotent on an empty list */
    clear_list(&l);
    CHECK(first_elem(&l) == NULL && last_elem(&l) == NULL && first_node(&l) == NULL && last_node(&l) == NULL);
    CHECK(locate_elem(&l, &i) == NULL);
}

static void test_single_element_and_ends(void) {
    struct list l;
    int a = 1, b = 2, c = 3;
    struct list_node *na, *nb, *nc;
    fault_reset();
    init_list(&l, 4);
    na = add_elem(&l, &a);
    CHECK(l.first == na && l.last == na && na->next == NULL && na->previous == NULL);
    delete_node(&l, na);
    CHECK(l.first == NULL && l.last == NULL && l.count == 0);
    na = add_elem(&l, &a);
    nb = add_elem(&l, &b);
    nc = add_elem(&l, &c);
    delete_node(&l, nb); /* middle */
    CHECK(na->next == nc && nc->previous == na && l.count == 2);
    delete_node(&l, nc); /* last */
    CHECK(l.last == na && na->next == NULL);
    nb = add_elem(&l, &b);
    delete_node(&l, na); /* first */
    CHECK(l.first == nb && nb->previous == NULL);
    clear_list(&l);
    CHECK(fault_outstanding() == 0);
}

static void test_allocation_failure(void) {
    struct list l;
    int a = 1, b = 2;
    long n;
    fault_reset();
    init_list(&l, 4);
    CHECK(add_elem(&l, &a) != NULL);
    for (n = 0; n < 1; n++) { /* the only allocation add_elem makes */
        fault_fail_after(n);
        CHECK(add_elem(&l, &b) == NULL);
        CHECK(l.count == 1 && l.first == l.last && l.first->data == &a && l.first->next == NULL);
    }
    fault_fail_after(1000000);
    CHECK(add_elem(&l, &b) != NULL && l.count == 2);
    clear_list(&l);
    CHECK(fault_outstanding() == 0);
}

int main(void) {
    test_random_operations();
    test_destroy_variants();
    test_single_element_and_ends();
    test_allocation_failure();
    return 0;
}
