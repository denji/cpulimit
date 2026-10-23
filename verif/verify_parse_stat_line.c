/* parse_proc_stat() on every well-formed line of one fixed shape: pid 1, a command name of one
 * arbitrary non-NUL byte, a state letter, and 19 numeric fields (4 to 22) of ONE symbolic digit
 * each. The bytes that make the line well formed are constants, so the solver only has to reason
 * about the digits, which keeps the formula small. Properties (the functional specification of
 * the accepting path):
 *   - the line is accepted;
 *   - state, ppid (field 4), utime (14), stime (15) and starttime (22) hold exactly the digits at
 *     those field positions, and no other field is mixed in;
 *   - memory safety and absence of overflow on the way (CBMC --bounds-check --pointer-check
 *     --signed-overflow-check --unsigned-overflow-check --conversion-check).
 * Numbers longer than one digit and malformed lines are the business of verify_parse_stat.c, the
 * fuzzer and the unit tests; this harness is about field positions. */
#include "harness.h"
#include "platform/procfs_parse.h"

#ifdef VERIF_CBMC
int main(void)
#else
static void one(void)
#endif
{
    char buf[48];
    unsigned char digit[23];   /* digit[k] is the value of field k, 4 <= k <= 22 */
    char name;
    unsigned char state;
    struct proc_stat out;
    size_t n = 0;
    int k;
    name = nondet_char();
    state = nondet_upper();
    VERIF_ASSUME(name != 0);
    buf[n++] = '1';
    buf[n++] = ' ';
    buf[n++] = '(';
    buf[n++] = name;
    buf[n++] = ')';
    buf[n++] = ' ';
    buf[n++] = (char)state;
    for (k = 4; k <= 22; k++) {
        digit[k] = nondet_digit();
        buf[n++] = ' ';
        buf[n++] = (char)('0' + digit[k]);
    }
    buf[n] = '\0';
    assert(n == 45);
    memset(&out, 0, sizeof(out));
    assert(parse_proc_stat(buf, &out) == 0);
    assert(out.state == (char)state);
    assert(out.ppid == (pid_t)digit[4]);
    assert(out.utime == digit[14]);
    assert(out.stime == digit[15]);
    assert(out.starttime == digit[22]);
#ifdef VERIF_CBMC
    return 0;
#endif
}

#ifndef VERIF_CBMC
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 2000000, i;
    for (i = 0; i < n; i++) one();
    printf("verify_parse_stat_line: %ld samples, no property violated\n", n);
    return 0;
}
#endif
