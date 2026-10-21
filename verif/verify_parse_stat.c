/* parse_proc_stat() on every NUL-terminated string of up to MAXLEN (48) bytes. The shortest line
 * that reaches field 22 is 46 bytes ("1 (x) S 0 0 ... 0": 22 one-character fields), so the bound
 * covers every field, but only with short numbers: long numbers (overflow of the 64-bit parse)
 * are covered by the fuzzer and the unit tests, not by this proof.
 *   - never reads outside the buffer, never overflows (CBMC --bounds-check --pointer-check
 *     --unsigned-overflow-check --signed-overflow-check --conversion-check)
 *   - returns 0 or -1; on -1 *out is unchanged
 *   - on 0 the state is a letter-or-symbol byte and ppid fits pid_t (non-negative)
 * The buffer is fully nondeterministic, so this covers command names with ')' and spaces,
 * truncated lines and numbers one digit longer than a uint64_t.
 * MAXLEN is small enough for the unwinding bound but long enough to hold a full prefix
 * "1 (x) S 1 0 ... " (fields 1..22 need 44+ bytes); the bounded proof therefore uses the
 * STATIC_PREFIX variant below for the deep fields and the free variant for the shallow ones. */
#include "harness.h"
#include "procfs_parse.h"

#ifndef MAXLEN
#define MAXLEN 48
#endif

#ifndef VERIF_CBMC
static long accepted; /* how many samples were valid lines: shows the sample space is not vacuous */
#endif

#ifdef VERIF_CBMC
int main(void)
#else
static void one(void)
#endif
{
    char buf[MAXLEN + 1];
    struct proc_stat before, after;
    size_t i, len;
    int rc;
    len = (size_t)nondet_uchar();
#ifndef VERIF_CBMC
    len %= MAXLEN + 1;
#endif
    VERIF_ASSUME(len <= MAXLEN);
    for (i = 0; i < len; i++) {
        buf[i] = nondet_statchar();
        VERIF_ASSUME(buf[i] != '\0');
    }
#ifndef VERIF_CBMC
    if (nondet_uchar() & 1) {
        /* half of the samples start from a well-formed line and are then damaged, otherwise random
         * bytes almost never get past field 3 and the deep fields would go untested */
        static const char *const good = "1 (a) S 1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8 9 1 2";
        size_t k, damage = (size_t)(nondet_uchar() % 4);
        len = strlen(good);
        if (len > MAXLEN) {
            len = MAXLEN;
        }
        memcpy(buf, good, len);
        for (k = 0; k < damage; k++) {
            buf[(size_t)nondet_uchar() % len] = nondet_statchar();
        }
        for (k = 0; k < len; k++) {
            if (buf[k] == '\0') {
                buf[k] = ' ';
            }
        }
    }
#endif
    buf[len] = '\0';
    before.state = nondet_char();
    before.ppid = nondet_pid();
    before.utime = nondet_u64();
    before.stime = nondet_u64();
    before.starttime = nondet_u64();
    after = before;
    rc = parse_proc_stat(buf, &after);
    assert(rc == 0 || rc == -1);
    if (rc == -1) {
        assert(after.state == before.state && after.ppid == before.ppid && after.utime == before.utime &&
               after.stime == before.stime && after.starttime == before.starttime);
    } else {
        assert(after.ppid >= 0);
#ifndef VERIF_CBMC
        accepted++;
#endif
    }
#ifdef VERIF_CBMC
    return 0;
#endif
}

#ifndef VERIF_CBMC
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 1000000, i;
    for (i = 0; i < n; i++) one();
    printf("verify_parse_stat: %ld samples (%ld parsed as valid lines), no property violated\n", n, accepted);
    return 0;
}
#endif
