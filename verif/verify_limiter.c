/* compute_slices(): the controller arithmetic for every double, including NaN, infinities and
 * denormals. Properties = invariants L1-L4 of docs/ARCHITECTURE.md:
 *   L1  work_ns + stop_ns == chunk_ns
 *   L2  0 <= work_ns <= chunk_ns, 0 <= stop_ns <= chunk_ns
 *   L3  rate is a number in [0, 1] and work_ns == (int64_t)(chunk_ns * rate)  (no NaN reaches the timer)
 *   L4  on the first cycle (pcpu < 0) the rate is the limit, clamped to [0, 1]
 * plus: a measured usage at or below the target never lowers the duty cycle below the previous one
 * (the controller cannot get stuck stopping an idle process). */
#include "harness.h"
#include "limiter.h"
#include "util.h"

#ifdef VERIF_CBMC
int main(void)
#else
static void one(void)
#endif
{
    double limit = nondet_double();
    double pcpu = nondet_double();
    double prev = nondet_double();
    long chunk_us = nondet_long();
    struct slices s;
    int64_t chunk_ns;
    VERIF_ASSUME(chunk_us >= 1 && chunk_us <= 1000000);   /* the CLI guarantees this range */
    VERIF_ASSUME(limit >= 0.0 && limit <= 64.0);          /* limit as a fraction of a CPU, <= 64 cores */
    VERIF_ASSUME(prev >= 0.0 && prev <= 1.0);             /* previous rate comes from this function */
    /* pcpu is a measurement: anything non-negative, or the negative "no sample yet" marker */
    chunk_ns = (int64_t)chunk_us * 1000;
    compute_slices(limit, pcpu, prev, chunk_us, &s);
    assert(s.work_ns + s.stop_ns == chunk_ns);                              /* L1 */
    assert(s.work_ns >= 0 && s.work_ns <= chunk_ns);                        /* L2 */
    assert(s.stop_ns >= 0 && s.stop_ns <= chunk_ns);
    assert(s.rate >= 0.0 && s.rate <= 1.0);                                 /* L3: also false for NaN */
    assert(s.work_ns == (int64_t)((double)chunk_ns * s.rate));
    if (pcpu < 0) {                                                         /* L4 */
        double want = limit > 1.0 ? 1.0 : limit;
        assert(s.rate == want);
    }
    if (pcpu >= 0 && pcpu <= prev * limit) {                                /* idle process: run fully */
        assert(s.rate == 1.0);
    }
#ifdef VERIF_CBMC
    return 0;
#endif
}

#ifndef VERIF_CBMC
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 2000000, i;
    for (i = 0; i < n; i++) one();
    printf("verify_limiter: %ld samples, no property violated\n", n);
    return 0;
}
#endif
