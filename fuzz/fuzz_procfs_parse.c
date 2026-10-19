/* Fuzz target for parse_proc_stat() and ticks_to_ms().
 *
 * Properties checked for every input, not just "does not crash":
 *   P1  the result is deterministic and the input is not modified
 *   P2  on failure *out is untouched
 *   P3  on success, rendering the fields back into a canonical stat line and parsing that
 *       gives the same fields (the parser has no hidden state)
 *   P4  ticks_to_ms() matches 128-bit arithmetic, saturating at INT64_MAX
 * Build with libFuzzer (make fuzz) or with the standalone driver (make fuzz-standalone). */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procfs_parse.h"

#define FAIL(msg) do { fprintf(stderr, "fuzz_procfs_parse: %s (line %d)\n", msg, __LINE__); abort(); } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *buf = malloc(size + 1); /* exact size: ASan sees any read past the NUL */
    char *copy;
    struct proc_stat a, b, c;
    char line[256];
    int ra, rb;
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    copy = malloc(size + 1);
    if (copy == NULL) {
        free(buf);
        return 0;
    }
    memcpy(copy, buf, size + 1);

    memset(&a, 0x5a, sizeof(a));
    memset(&b, 0x5a, sizeof(b));
    ra = parse_proc_stat(buf, &a);
    rb = parse_proc_stat(buf, &b);
    if (ra != rb || memcmp(buf, copy, size + 1) != 0) {
        FAIL("P1 not deterministic or input modified");
    }
    if (ra != 0) {
        struct proc_stat untouched;
        memset(&untouched, 0x5a, sizeof(untouched));
        if (ra != -1 || memcmp(&a, &untouched, sizeof(a)) != 0) {
            FAIL("P2 failure must return -1 and leave *out alone");
        }
    } else {
        snprintf(line, sizeof(line),
                 "1 (x) %c %ld 0 0 0 0 0 0 0 0 0 %" PRIu64 " %" PRIu64 " 0 0 0 0 0 0 %" PRIu64 " 0\n",
                 a.state, (long)a.ppid, a.utime, a.stime, a.starttime);
        memset(&c, 0, sizeof(c));
        if (parse_proc_stat(line, &c) != 0 || c.state != a.state || c.ppid != a.ppid ||
            c.utime != a.utime || c.stime != a.stime || c.starttime != a.starttime) {
            FAIL("P3 canonical re-parse differs");
        }
    }

    if (size >= 9) {
        uint64_t ticks;
        long tck = (long)(int8_t)data[8] * 25 + 1; /* includes <= 0 and ordinary values */
        if (size >= 17) { /* ... or any value at all, to reach the overflow-avoiding branch */
            uint64_t w;
            memcpy(&w, data + 9, sizeof(w));
            tck = (long)(w >> (data[8] & 63));
        }
        unsigned __int128 want;
        int64_t got;
        memcpy(&ticks, data, sizeof(ticks));
        got = ticks_to_ms(ticks, tck);
        if (tck <= 0) {
            want = 0;
        } else {
            want = (unsigned __int128)ticks * 1000u / (unsigned long)tck;
            if (want > (unsigned __int128)INT64_MAX) {
                want = (unsigned __int128)INT64_MAX;
            }
        }
        if (got < 0 || (unsigned __int128)got != want) {
            FAIL("P4 ticks_to_ms differs from 128-bit arithmetic");
        }
    }
    free(copy);
    free(buf);
    return 0;
}
