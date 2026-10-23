/* Common layer for the verification harnesses in this directory.
 *
 * Each harness states a property over ALL inputs of a bounded shape, with nondeterministic
 * inputs (nondet_*) and __CPROVER_assume() for preconditions:
 *
 *   - under CBMC (cbmc --unwinding N --bounds-check --pointer-check --signed-overflow-check ...)
 *     the nondet_* functions are free values and the assertions are PROVED for the bound;
 *   - without CBMC (make verif-sim) the same harness is compiled as an ordinary program: the
 *     nondet_* functions draw from a PRNG with a bias to boundary values, assume() discards the
 *     sample and assert() is a plain assert. That is random testing of the SAME property: it
 *     finds a violated property and shows the harness itself is sound, it proves nothing.
 *
 * The mode is chosen by VERIF_CBMC, which tools/verif.sh passes to cbmc with -D. (CBMC does not
 * reliably predefine a macro of its own for this, and a harness that silently compiled the PRNG
 * branch under cbmc would "prove" a single fixed pseudo-random run.)
 *
 * Keep the two modes honest: a harness must not do anything that only makes sense in one. */
#ifndef CPULIMIT_VERIF_HARNESS_H
#define CPULIMIT_VERIF_HARNESS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#ifdef VERIF_CBMC
unsigned char nondet_uchar(void);
#define nondet_statchar nondet_char
uint64_t nondet_u64(void);
int64_t nondet_i64(void);
long nondet_long(void);
char nondet_char(void);
pid_t nondet_pid(void);
double nondet_double(void);
#define VERIF_ASSUME(c) __CPROVER_assume(c)
/* a value from a small range: constrained by an assumption here, drawn directly in the simulation
 * (an assumption on a random byte would discard nearly every sample and test nothing) */
static unsigned char nondet_digit(void) { unsigned char d = nondet_uchar(); __CPROVER_assume(d <= 9); return d; }
static unsigned char nondet_upper(void) { unsigned char c = nondet_uchar(); __CPROVER_assume(c >= 'A' && c <= 'Z'); return c; }
#else
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t verif_state = 88172645463325252ULL;
#define VERIF_UNUSED __attribute__((unused))
static VERIF_UNUSED uint64_t verif_next(void) {
    verif_state ^= verif_state << 13;
    verif_state ^= verif_state >> 7;
    verif_state ^= verif_state << 17;
    return verif_state;
}
/* every few draws pick a boundary value, otherwise a value of random magnitude */
static VERIF_UNUSED uint64_t verif_u64(void) {
    static const uint64_t edge[] = { 0, 1, 2, 9, 10, 99, 100, 255, 1000, 1000000, 4294967295ULL,
        4294967296ULL, 9223372036854775807ULL, 9223372036854775808ULL, 18446744073709551615ULL,
        1844674407370955161ULL, 18446744073709551ULL };
    uint64_t r = verif_next();
    if (r % 4 == 0) {
        return edge[(r >> 8) % (sizeof(edge) / sizeof(edge[0]))] + (r >> 40) % 3 - 1;
    }
    return verif_next() >> (verif_next() % 64);
}
static VERIF_UNUSED unsigned char nondet_uchar(void) { return (unsigned char)verif_next(); }
static VERIF_UNUSED uint64_t nondet_u64(void) { return verif_u64(); }
static VERIF_UNUSED int64_t nondet_i64(void) {
    uint64_t v = verif_u64();
    return (verif_next() & 1) ? (int64_t)v : (int64_t)(0 - v); /* unsigned negate: no signed overflow */
}
/* same-width reinterpretation, no value conversion (a narrowing conversion is what
 * --conversion-check is there to flag in the code under test, not in the harness) */
static VERIF_UNUSED char nondet_char(void) { unsigned char u = (unsigned char)verif_next(); char c; memcpy(&c, &u, 1); return c; }
static VERIF_UNUSED pid_t nondet_pid(void) { uint64_t u = verif_next(); pid_t p; memcpy(&p, &u, sizeof(p)); return p; }
static VERIF_UNUSED long nondet_long(void) { return (long)nondet_i64(); }
/* a byte from the alphabet of a stat line, so random strings reach the deep fields too */
static VERIF_UNUSED char nondet_statchar(void) {
    static const char alpha[] = " ()0123456789 0123456789 SRZ-";
    uint64_t r = verif_next();
    if ((r & 7) == 0) {
        return nondet_char();
    }
    return alpha[(r >> 8) % (sizeof(alpha) - 1)];
}
static VERIF_UNUSED double nondet_double(void) {
    static const double edge[] = { 0.0, -0.0, 1.0, 0.5, 100.0, 1e-300, 4.9e-324, 1e300, 1.7976931348623157e308 };
    uint64_t r = verif_next();
    if (r % 8 == 0) return NAN;
    if (r % 8 == 1) return INFINITY * ((r >> 8) & 1 ? 1 : -1);
    if (r % 8 == 2) return edge[(r >> 8) % (sizeof(edge) / sizeof(edge[0]))];
    return (double)(verif_next() >> 11) / (double)(1ULL << 53) * pow(10.0, (double)((int)(verif_next() % 40) - 20));
}
static VERIF_UNUSED unsigned char nondet_digit(void) { return (unsigned char)(verif_next() % 10); }
static VERIF_UNUSED unsigned char nondet_upper(void) { return (unsigned char)('A' + verif_next() % 26); }
/* an unsatisfied assumption discards this sample (the caller returns) */
#define VERIF_ASSUME(c) do { if (!(c)) return; } while (0)
#endif

#endif
