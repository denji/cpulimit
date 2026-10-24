/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* ticks_to_ms(): exact for every input, saturating at INT64_MAX, 0 for an unusable rate.
 * The specification is the one-line 128-bit formula; CBMC supports __int128. */
#include "harness.h"
#include "platform/procfs_parse.h"

#ifdef VERIF_CBMC
int main(void)
#else
static void one(void)
#endif
{
    uint64_t ticks = nondet_u64();
    long hz = nondet_long();
    int64_t got;
#ifdef VERIF_CBMC
#ifndef VERIF_TICKS_FULL
    /* Bounded domain for the model checker: ticks below 2^48 (89 years at 100 Hz) and clock rates up
     * to 2^20 (real CLK_TCK values are 100 to 1000; 2^20 is generous). The upper bits are then
     * constant, which keeps the 64-bit dividers and the 128-bit multipliers small enough for the
     * solver: the first runs, over the full range, never finished building the formula. What this
     * does not cover is the saturation at INT64_MAX and the 1000-iteration path for clock rates above
     * 1.8e16: those are covered by the full-range simulation (make verif-sim) and by
     * test/unit/procfs_parse_test.c. Compile with -DVERIF_TICKS_FULL to try the full range. */
    VERIF_ASSUME(ticks < ((uint64_t)1 << 48));
    VERIF_ASSUME(hz <= (1L << 20));
#else
    /* above UINT64_MAX / 1000 the code loops 1000 times; unwind 3 cannot hold that */
    VERIF_ASSUME(hz <= 18446744073709551L);
#endif
#endif
    got = ticks_to_ms(ticks, hz);
    assert(got >= 0);
    if (hz <= 0) {
        assert(got == 0);
    } else {
        /* The specification is "floor(ticks * 1000 / hz), saturated at INT64_MAX". Stated with
         * multiplications only, which the solver handles far better than a 128-bit division:
         *     got * hz <= ticks * 1000 < (got + 1) * hz          when not saturated,
         *     got == INT64_MAX and (INT64_MAX) * hz <= ticks * 1000   when saturated. */
        unsigned __int128 num = (unsigned __int128)ticks * 1000u;
        unsigned __int128 h = (unsigned __int128)(uint64_t)hz;
        if (got == INT64_MAX) {
            assert((unsigned __int128)INT64_MAX * h <= num);
        } else {
            assert((unsigned __int128)got * h <= num);
            assert(num < ((unsigned __int128)got + 1u) * h);
        }
    }
#ifdef VERIF_CBMC
    return 0;
#endif
}

#ifndef VERIF_CBMC
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 2000000, i;
    for (i = 0; i < n; i++) one();
    printf("verify_ticks: %ld samples, no property violated\n", n);
    return 0;
}
#endif
