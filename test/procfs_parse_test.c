#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "procfs_parse.h"

/* fields 5..13 and 16..21 are filler; the ones that matter are ppid, utime, stime, starttime */
#define TAIL_FROM_PGRP " 1234 1234 34816 1234 4194304 100 0 0 0 "
#define TAIL_AFTER_STIME " 0 0 20 0 1 0 "

static void make_stat(char *dst, size_t size, const char *head, const char *state, const char *ppid,
                      const char *utime, const char *stime, const char *start) {
    snprintf(dst, size, "%s %s %s%s%s %s%s%s 12345678 1000 18446744073709551615\n", head, state, ppid,
             TAIL_FROM_PGRP, utime, stime, TAIL_AFTER_STIME, start);
}

static void test_typical(void) {
    struct proc_stat st;
    char line[512];
    make_stat(line, sizeof(line), "1234 (bash)", "S", "1000", "5", "7", "98765");
    CHECK(parse_proc_stat(line, &st) == 0);
    CHECK(st.state == 'S');
    CHECK(st.ppid == 1000);
    CHECK(st.utime == 5);
    CHECK(st.stime == 7);
    CHECK(st.starttime == 98765);
}

/* The command name is attacker-controlled text. The old strtok() parser split it on spaces and
 * mis-assigned every field after it. */
static void test_hostile_command_names(void) {
    static const char *heads[] = {
        "42 (a b)",
        "42 (a b c d e f g)",
        "42 (Web Content)",
        "42 (x) R 99 99 99 99)",           /* a fake ") R 99 ..." inside the name */
        "42 (a) Z 1 (b)",
        "42 ()",
        "42 (((()))",
        "42 (a\tb)",
        "42 (name with ) paren and ( more)",
        NULL
    };
    int i;
    for (i = 0; heads[i] != NULL; i++) {
        struct proc_stat st;
        char line[512];
        make_stat(line, sizeof(line), heads[i], "R", "7", "11", "13", "4242");
        CHECK(parse_proc_stat(line, &st) == 0);
        CHECK(st.state == 'R');
        CHECK(st.ppid == 7);
        CHECK(st.utime == 11 && st.stime == 13 && st.starttime == 4242);
    }
}

static void test_zombie_state_is_reported(void) {
    struct proc_stat st;
    char line[512];
    make_stat(line, sizeof(line), "9 (defunct)", "Z", "1", "0", "0", "1");
    CHECK(parse_proc_stat(line, &st) == 0 && st.state == 'Z');
}

static void test_limits(void) {
    struct proc_stat st;
    char line[512];
    /* largest 64-bit values are accepted, one more is rejected */
    make_stat(line, sizeof(line), "1 (x)", "S", "2147483647", "18446744073709551615", "0", "18446744073709551615");
    CHECK(parse_proc_stat(line, &st) == 0);
    CHECK(st.ppid == 2147483647 && st.utime == UINT64_MAX && st.starttime == UINT64_MAX);
    make_stat(line, sizeof(line), "1 (x)", "S", "2", "18446744073709551616", "0", "1");
    CHECK(parse_proc_stat(line, &st) == -1);
    make_stat(line, sizeof(line), "1 (x)", "S", "2", "0", "99999999999999999999999999", "1");
    CHECK(parse_proc_stat(line, &st) == -1);
    make_stat(line, sizeof(line), "1 (x)", "S", "2147483648", "0", "0", "1");
    CHECK(parse_proc_stat(line, &st) == -1);
}

static void test_rejects_malformed(void) {
    struct proc_stat st;
    char line[512];
    /* After the command name come the fields 3..22: the state, then 19 numbers. The numbers
     * below are 2..20, so ppid (field 4) is 2, utime (field 14) is 12, stime (field 15) is 13
     * and starttime (field 22) is 20. Every bad line differs from this control by one defect. */
    static const char *control = "1 (x) S 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20";
    static const char *bad[] = {
        "",
        "\n",
        "no parenthesis at all",
        "1 (x)",
        "1 (x) S",
        "1 (x) S 2",
        "1 (x) S 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19",    /* starttime missing */
        "1 (x) SS 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20",  /* two-character state */
        "1 (x) S abc 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20", /* non-numeric ppid */
        "1 (x) S -2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20",  /* negative ppid */
        "1 (x) S 2 3 4 5 6 7 8 9 10 11 -12 13 14 15 16 17 18 19 20",  /* negative utime */
        "1 (x) S 2 3 4 5 6 7 8 9 10 11 12 1x3 14 15 16 17 18 19 20",  /* garbage in stime */
        "1 (x) S 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 +20",  /* sign in starttime */
        "1 (x) S 2 3 4 5 6 7 8 9 10\n11 12 13 14 15 16 17 18 19 20",  /* newline inside the data */
        NULL
    };
    int i;
    CHECK(parse_proc_stat(control, &st) == 0);  /* the fixtures themselves are valid */
    CHECK(st.ppid == 2 && st.utime == 12 && st.stime == 13 && st.starttime == 20);
    memset(&st, 0x5a, sizeof(st));
    for (i = 0; bad[i] != NULL; i++) {
        struct proc_stat probe = st;
        CHECK(parse_proc_stat(bad[i], &probe) == -1);
        CHECK(memcmp(&probe, &st, sizeof(st)) == 0);  /* untouched on failure */
    }
    CHECK(parse_proc_stat(NULL, &st) == -1);
    /* a valid line cut at every possible length must either parse or be rejected, never crash */
    make_stat(line, sizeof(line), "7 (cut)", "S", "3", "1", "2", "3");
    {
        size_t len = strlen(line);
        size_t n;
        char cut[512];
        for (n = 0; n <= len; n++) {
            memcpy(cut, line, n);
            cut[n] = '\0';
            (void)parse_proc_stat(cut, &st);
        }
    }
}

static void test_ticks_to_ms(void) {
    CHECK(ticks_to_ms(0, 100) == 0);
    CHECK(ticks_to_ms(1, 100) == 10);
    CHECK(ticks_to_ms(150, 100) == 1500);
    CHECK(ticks_to_ms(3, 1000) == 3);
    CHECK(ticks_to_ms(7, 128) == 54);                  /* 7000 / 128 = 54.68..., truncated like the old code */
    CHECK(ticks_to_ms(12345, 0) == 0);                 /* unusable clock rate */
    CHECK(ticks_to_ms(12345, -5) == 0);
    CHECK(ticks_to_ms(UINT64_MAX, 100) == INT64_MAX);  /* saturates instead of wrapping */
    CHECK(ticks_to_ms((uint64_t)INT64_MAX, 1) == INT64_MAX);
    /* hz so large that rest * 1000 overflows 64 bits: the answer is still exact */
    CHECK(ticks_to_ms((uint64_t)LONG_MAX / 2, LONG_MAX) == 499);   /* 0.4999.. s */
    CHECK(ticks_to_ms((uint64_t)LONG_MAX - 1, LONG_MAX) == 999);
    CHECK(ticks_to_ms((uint64_t)LONG_MAX, LONG_MAX) == 1000);
    CHECK(ticks_to_ms((uint64_t)LONG_MAX + (uint64_t)LONG_MAX / 4, LONG_MAX) == 1249);
    /* exact against the naive formula wherever the naive formula cannot overflow */
    {
        uint64_t t;
        for (t = 0; t < 100000; t += 37) {
            CHECK(ticks_to_ms(t, 100) == (int64_t)(t * 1000 / 100));
            CHECK(ticks_to_ms(t, 250) == (int64_t)(t * 1000 / 250));
            CHECK(ticks_to_ms(t, 1024) == (int64_t)(t * 1000 / 1024));
        }
    }
}

/* the parser against the real kernel */
static void test_live_proc(void) {
#ifdef __linux__
    struct proc_stat st;
    char buf[1024];
    FILE *f = fopen("/proc/self/stat", "r");
    size_t n;
    CHECK(f != NULL);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    CHECK(n > 0);
    buf[n] = '\0';
    CHECK(parse_proc_stat(buf, &st) == 0);
    CHECK(st.ppid == getppid());
    CHECK(st.state == 'R' || st.state == 'S');
    CHECK(st.starttime > 0);
#endif
}

int main(void) {
    test_typical();
    test_hostile_command_names();
    test_zombie_state_is_reported();
    test_limits();
    test_rejects_malformed();
    test_ticks_to_ms();
    test_live_proc();
    return 0;
}
