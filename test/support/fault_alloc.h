/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* Allocator used by the group/list tests (see src/core/alloc.h): it can be told to fail
 * from the n-th allocation on, and it counts live allocations so a leak is a plain
 * failed CHECK instead of something only a sanitizer could notice. */
#ifndef FAULT_ALLOC_H
#define FAULT_ALLOC_H

#include <stddef.h>

void *fault_malloc(size_t n);
void *fault_realloc(void *p, size_t n);
void fault_free(void *p);

/* forget all counters and cancel any scheduled failure */
void fault_reset(void);
/* the n-th allocation from now (0 = the very next one) and every later one fails */
void fault_fail_after(long n);
/* allocations made and not yet freed */
long fault_outstanding(void);
/* allocations attempted since the last fault_reset() */
long fault_total(void);

#define CL_MALLOC(n) fault_malloc(n)
#define CL_REALLOC(p, n) fault_realloc((p), (n))
#define CL_FREE(p) fault_free(p)

#endif
