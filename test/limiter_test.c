#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "check.h"
#include "limiter.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* The controller arithmetic exactly as it was in limit_process() before it was
 * extracted. Used as a regression oracle on the domain where the old code was
 * well defined (limit <= 1, work slice < 1 s). */
static void old_slices(double limit, double pcpu, double workingrate, int chunk_size,
                       long *twork_ns, long *tsleep_ns, double *rate_out) {
    if (pcpu < 0) {
        pcpu = limit;
        workingrate = limit;
        *twork_ns = chunk_size * limit * 1000;
    } else {
        workingrate = MIN(workingrate / pcpu * limit, 1);
        *twork_ns = chunk_size * 1000 * workingrate;
    }
    *tsleep_ns = chunk_size * 1000 - *twork_ns;
    *rate_out = workingrate;
}

static const double LIMITS[] = { 0, 1e-9, 0.01, 0.1, 0.5, 0.99, 1, 1.5, 2, 8, 64, 1024, 1e6 };
/* includes what a long-idle process decays to: tiny normal numbers, then denormals, then 0 */
static const double PCPUS[] = { -1, 0, 5e-324, 1e-310, 1e-300, 1e-12, 1e-9, 1e-6, 0.001, 0.1, 0.5, 1, 2, 64, 1e9 };
static const double PREVS[] = { 0, 1e-9, 0.25, 0.5, 1, 1.5, 100 };
static const long CHUNKS[] = { 1, 2, 999, 100000, 999999, 1000000 };
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Postconditions documented in limiter.h hold for every combination. */
static void test_invariants(void) {
    int a, b, c, d;
    long checked = 0;
    for (a = 0; a < N(LIMITS); a++) {
        for (b = 0; b < N(PCPUS); b++) {
            for (c = 0; c < N(PREVS); c++) {
                for (d = 0; d < N(CHUNKS); d++) {
                    struct slices s;
                    int64_t chunk_ns = (int64_t)CHUNKS[d] * 1000;
                    compute_slices(LIMITS[a], PCPUS[b], PREVS[c], CHUNKS[d], &s);
                    CHECK(s.work_ns >= 0);
                    CHECK(s.stop_ns >= 0);
                    CHECK(s.work_ns <= chunk_ns);
                    CHECK(s.work_ns + s.stop_ns == chunk_ns);
                    CHECK(s.rate >= 0 && s.rate <= 1);
                    CHECK(!isnan(s.rate));
                    checked++;
                }
            }
        }
    }
    CHECK(checked == (long)N(LIMITS) * N(PCPUS) * N(PREVS) * N(CHUNKS));
}

/* compute_slices() must not depend on IEEE inf/NaN results: no division by zero,
 * no invalid operation and no overflow may even be raised, for any input. */
static void test_no_fp_exceptions(void) {
    int a, b, c, d;
    volatile double zero = 0.0, sink;
    /* Probe that this environment reports FP exceptions at all (valgrind and some emulators
     * do not). If it does not, say so instead of passing without having checked anything. */
    feclearexcept(FE_ALL_EXCEPT);
    sink = 1.0 / zero;
    (void)sink;
    if (!fetestexcept(FE_DIVBYZERO)) {
        fprintf(stderr, "note: FP exception flags are not reported here; test_no_fp_exceptions skipped\n");
        return;
    }
    feclearexcept(FE_ALL_EXCEPT);
    for (a = 0; a < N(LIMITS); a++) {
        for (b = 0; b < N(PCPUS); b++) {
            for (c = 0; c < N(PREVS); c++) {
                for (d = 0; d < N(CHUNKS); d++) {
                    struct slices s;
                    compute_slices(LIMITS[a], PCPUS[b], PREVS[c], CHUNKS[d], &s);
                }
            }
        }
    }
    CHECK(fetestexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW) == 0);
}

/* No regression against the previous implementation where it was well defined. */
static void test_matches_old_formula(void) {
    int a, b, c, d;
    long compared = 0, excluded = 0;
    for (a = 0; a < N(LIMITS); a++) {
        if (LIMITS[a] > 1) {
            continue;
        }
        for (b = 0; b < N(PCPUS); b++) {
            for (c = 0; c < N(PREVS); c++) {
                if (PREVS[c] > 1) {
                    continue;
                }
                for (d = 0; d < N(CHUNKS); d++) {
                    struct slices s;
                    long ow, os;
                    double orate;
                    if (CHUNKS[d] >= 1000000) {
                        continue;  /* old code: tv_nsec == 1e9, rejected by nanosleep() */
                    }
                    if (PCPUS[b] >= 0 && PCPUS[b] < 1e-300) {
                        /* idle (0) or denormal usage: the old formula divided by zero or
                         * overflowed and its answer came from an inf/NaN comparison. Not a
                         * behavior worth preserving; test_invariants covers these inputs.
                         * The domain is stated explicitly, not discovered from FP flags, so
                         * the test means the same on every platform and under valgrind. */
                        excluded++;
                        continue;
                    }
                    old_slices(LIMITS[a], PCPUS[b], PREVS[c], (int)CHUNKS[d], &ow, &os, &orate);
                    compute_slices(LIMITS[a], PCPUS[b], PREVS[c], CHUNKS[d], &s);
                    /* different multiplication order may differ by one truncated nanosecond */
                    CHECK(s.work_ns - ow <= 1 && ow - s.work_ns <= 1);
                    CHECK(s.stop_ns - os <= 1 && os - s.stop_ns <= 1);
                    CHECK(fabs(s.rate - orate) < 1e-12);
                    compared++;
                }
            }
        }
    }
    CHECK(compared > 1000);
    CHECK(excluded > 0);
}

/* A busy target (above the limit) is throttled more; an idle one is released. */
static void test_direction(void) {
    struct slices over, under, idle;
    compute_slices(0.5, 1.0, 0.8, 100000, &over);   /* using twice the limit */
    compute_slices(0.5, 0.25, 0.4, 100000, &under); /* using half the limit */
    compute_slices(0.5, 0.0, 0.4, 100000, &idle);
    CHECK(over.rate < 0.8);
    CHECK(under.rate > 0.4);
    CHECK(idle.rate == 1 && idle.stop_ns == 0);
}

/* First cycle with the limit: work/stop split follows the limit. */
/* A bad measurement must never reach nanosleep(): NaN anywhere gives a full work slice (the
 * safe side: the target is not held stopped on account of garbage). */
static void test_nan_inputs(void) {
    struct slices s;
    compute_slices(0.5, NAN, 0.5, 100000, &s);
    CHECK(s.rate == 1.0 && s.work_ns == 100000000 && s.stop_ns == 0);
    compute_slices(NAN, 0.4, 0.5, 100000, &s);
    CHECK(s.rate == 1.0 && s.stop_ns == 0);
    compute_slices(0.5, 0.4, NAN, 100000, &s);
    CHECK(s.rate == 1.0 && s.stop_ns == 0);
}

static void test_first_cycle(void) {
    struct slices s;
    compute_slices(0.25, -1, 0, 100000, &s);
    CHECK(s.work_ns == 25000000 && s.stop_ns == 75000000);
    compute_slices(0, -1, 0, 100000, &s);
    CHECK(s.work_ns == 0 && s.stop_ns == 100000000);
    /* limit above one CPU on the first cycle: never longer than the chunk */
    compute_slices(4, -1, 0, 100000, &s);
    CHECK(s.work_ns == 100000000 && s.stop_ns == 0);
    /* the value that used to produce an invalid nanosleep() argument */
    compute_slices(1, -1, 0, 1000000, &s);
    CHECK(s.work_ns == 1000000000LL && s.stop_ns == 0);
}

int main(void) {
    test_invariants();
    test_no_fp_exceptions();
    test_matches_old_formula();
    test_direction();
    test_first_cycle();
    test_nan_inputs();
    return 0;
}
