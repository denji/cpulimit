/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>

#include "support/fault_alloc.h"

static long countdown = -1;  /* < 0: never fail */
static long outstanding = 0;
static long total = 0;

static int should_fail(void) {
    total++;
    if (countdown < 0) {
        return 0;
    }
    if (countdown == 0) {
        return 1;
    }
    countdown--;
    return 0;
}

void *fault_malloc(size_t n) {
    void *p;
    if (should_fail()) {
        return NULL;
    }
    p = malloc(n);
    if (p != NULL) {
        outstanding++;
    }
    return p;
}

void *fault_realloc(void *p, size_t n) {
    void *q;
    if (p == NULL) {
        return fault_malloc(n);
    }
    if (should_fail()) {
        return NULL;  /* the original block stays valid, as with the real realloc() */
    }
    q = realloc(p, n);
    return q;
}

void fault_free(void *p) {
    if (p != NULL) {
        outstanding--;
    }
    free(p);
}

void fault_reset(void) {
    countdown = -1;
    outstanding = 0;
    total = 0;
}

void fault_fail_after(long n) {
    countdown = n;
}

long fault_outstanding(void) {
    return outstanding;
}

long fault_total(void) {
    return total;
}
