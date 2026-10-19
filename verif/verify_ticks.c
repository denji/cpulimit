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
        unsigned __int128 want = (unsigned __int128)ticks * 1000u / (uint64_t)hz;
        if (want > (unsigned __int128)INT64_MAX) {
            want = INT64_MAX;
        }
        assert((unsigned __int128)got == want);
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
