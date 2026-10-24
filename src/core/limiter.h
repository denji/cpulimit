/**
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012 Angelo Marletta
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

#ifndef CPULIMIT_LIMITER_H
#define CPULIMIT_LIMITER_H

#include <stdint.h>

/* How one control chunk is split between letting the target run and stopping it. */
struct slices {
    int64_t work_ns;  /* time the processes are allowed to run */
    int64_t stop_ns;  /* time the processes are stopped */
    double rate;      /* work_ns / chunk, the "active rate" */
};

/*
 * Pure controller step (no I/O, no globals), so it can be tested and verified
 * in isolation.
 *
 *   limit      allowed CPU as a fraction (1.0 == one full CPU), finite, >= 0
 *   pcpu       measured CPU use of the controlled processes over the last
 *              chunk, same unit; a negative value means "no measurement yet"
 *   prev_rate  rate returned by the previous call (ignored on the first call)
 *   chunk_us   chunk length in microseconds, 1..1000000
 *
 * Guarantees for every input above (checked by test/limiter_test.c):
 *   0 <= work_ns <= chunk_us * 1000
 *   work_ns + stop_ns == chunk_us * 1000
 *   0 <= rate <= 1
 */
void compute_slices(double limit, double pcpu, double prev_rate, long chunk_us, struct slices *out);

#endif
