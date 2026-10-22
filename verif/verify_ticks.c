/* ticks_to_ms(): exact for every input, saturating at INT64_MAX, 0 for an unusable rate.
 * The specification is the one-line 128-bit formula; CBMC supports __int128. */
#include "harness.h"
#include "procfs_parse.h"

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
    /* Above UINT64_MAX / 1000 ticks_to_ms() divides by 1000 repeated additions, a loop that makes
     * the formula too large for the solver (the first CBMC runs timed out). That path is covered by
     * the full-range simulation (make verif-sim) and by test/procfs_parse_test.c; the bounded proof
     * is for every clock rate below it. */
    VERIF_ASSUME(hz <= 18446744073709551L);
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
