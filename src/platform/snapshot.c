/**
 *
 * cpulimit - limit the CPU usage of a process
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

#include <stdlib.h>

#include "core/alloc.h"
#include "platform/snapshot.h"

struct edge {
    pid_t ppid;
    size_t idx;
};

static int edge_cmp(const void *a, const void *b) {
    const struct edge *x = (const struct edge *)a;
    const struct edge *y = (const struct edge *)b;
    if (x->ppid != y->ppid) {
        return x->ppid < y->ppid ? -1 : 1;
    }
    return x->idx < y->idx ? -1 : (x->idx > y->idx ? 1 : 0);
}

static struct snap_rec *rec_at(void *recs, size_t stride, size_t i) {
    return (struct snap_rec *)((char *)recs + i * stride);
}

int snapshot_select(void *recs, size_t n, size_t stride, pid_t target, int include_children) {
    size_t i, head = 0, tail = 0;
    struct edge *edges;
    size_t *queue;

    for (i = 0; i < n; i++) {
        struct snap_rec *r = rec_at(recs, stride, i);
        r->selected = (target == 0 || r->pid == target);
    }
    if (target == 0 || !include_children || n == 0) {
        return 0;
    }
    edges = (struct edge *)CL_MALLOC(n * sizeof(*edges));
    queue = (size_t *)CL_MALLOC(n * sizeof(*queue));
    if (edges == NULL || queue == NULL) {
        CL_FREE(edges);
        CL_FREE(queue);
        for (i = 0; i < n; i++) {
            rec_at(recs, stride, i)->selected = 0;
        }
        return -1;
    }
    for (i = 0; i < n; i++) {
        edges[i].ppid = rec_at(recs, stride, i)->ppid;
        edges[i].idx = i;
        if (rec_at(recs, stride, i)->selected) {
            queue[tail++] = i;
        }
    }
    qsort(edges, n, sizeof(*edges), edge_cmp);
    while (head < tail) {
        pid_t parent = rec_at(recs, stride, queue[head++])->pid;
        size_t lo = 0, hi = n;
        while (lo < hi) { /* first edge whose ppid >= parent */
            size_t mid = lo + (hi - lo) / 2;
            if (edges[mid].ppid < parent) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        for (; lo < n && edges[lo].ppid == parent; lo++) {
            struct snap_rec *child = rec_at(recs, stride, edges[lo].idx);
            if (!child->selected) {
                child->selected = 1;
                queue[tail++] = edges[lo].idx;
            }
        }
    }
    CL_FREE(edges);
    CL_FREE(queue);
    return 0;
}
