/* ticks_to_ms(): exact for every input, saturating at INT64_MAX, 0 for an unusable rate.
 * The specification is the one-line 128-bit formula; CBMC supports __int128. */
#include "harness.h"
#include "procfs_parse.h"

#ifdef __CPROVER
int main(void)
#else
static void one(void)
#endif
{
    uint64_t ticks = nondet_u64();
    long hz = nondet_long();
    int64_t got = ticks_to_ms(ticks, hz);
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
#ifdef __CPROVER
    return 0;
#endif
}

#ifndef __CPROVER
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 2000000, i;
    for (i = 0; i < n; i++) one();
    printf("verify_ticks: %ld samples, no property violated\n", n);
    return 0;
}
#endif
