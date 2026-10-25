/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "support/check.h"
#include "util/util.h"

static void test_parse_long_ok(void) {
    long v = -99;
    CHECK(parse_long("0", 0, 100, &v) == 0 && v == 0);
    CHECK(parse_long("50", 0, 100, &v) == 0 && v == 50);
    CHECK(parse_long("100", 0, 100, &v) == 0 && v == 100);
    CHECK(parse_long("+7", 0, 100, &v) == 0 && v == 7);
    CHECK(parse_long("-5", -10, 10, &v) == 0 && v == -5);
    CHECK(parse_long("007", 0, 100, &v) == 0 && v == 7);
}

static void test_parse_long_rejects(void) {
    long v = 1234;
    static const char *bad[] = { "", " 5", "5 ", "5x", "abc", "5.0", "0x10", "--5", "+", "-", "1 2", NULL };
    int i;
    for (i = 0; bad[i] != NULL; i++) {
        errno = 0;
        CHECK(parse_long(bad[i], -1000, 1000, &v) == -1);
        CHECK(errno == EINVAL);
        CHECK(v == 1234); /* untouched on failure */
    }
    errno = 0;
    CHECK(parse_long(NULL, 0, 10, &v) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(parse_long("101", 0, 100, &v) == -1 && errno == ERANGE);
    errno = 0;
    CHECK(parse_long("-1", 0, 100, &v) == -1 && errno == ERANGE);
    errno = 0;
    CHECK(parse_long("99999999999999999999999", LONG_MIN, LONG_MAX, &v) == -1 && errno == ERANGE);
    errno = 0;
    CHECK(parse_long("-99999999999999999999999", LONG_MIN, LONG_MAX, &v) == -1 && errno == ERANGE);
    CHECK(v == 1234);
}

static void test_parse_double(void) {
    double v = -1;
    static const char *bad[] = {
        "", " 1", "1 ", "1.5abc", "abc", "nan", "NaN", "inf", "-inf", "infinity", "1e999", NULL
    };
    int i;
    CHECK(parse_double("0", 0, 10, &v) == 0 && v == 0);
    CHECK(parse_double("0.25", 0, 10, &v) == 0 && v == 0.25);
    CHECK(parse_double("1e1", 0, 10, &v) == 0 && v == 10);
    CHECK(parse_double("10", 0, 10, &v) == 0 && v == 10);
    v = 77;
    for (i = 0; bad[i] != NULL; i++) {
        errno = 0;
        CHECK(parse_double(bad[i], -1e6, 1e6, &v) == -1);
        CHECK(v == 77);
    }
    errno = 0;
    CHECK(parse_double(NULL, 0, 1, &v) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(parse_double("-0.5", 0, 1, &v) == -1 && errno == ERANGE);
    errno = 0;
    CHECK(parse_double("1.5", 0, 1, &v) == -1 && errno == ERANGE);
    CHECK(v == 77);
}

static void test_ns_to_timespec(void) {
    struct timespec ts;
    ts = ns_to_timespec(0);
    CHECK(ts.tv_sec == 0 && ts.tv_nsec == 0);
    ts = ns_to_timespec(-5);
    CHECK(ts.tv_sec == 0 && ts.tv_nsec == 0);
    ts = ns_to_timespec(999999999LL);
    CHECK(ts.tv_sec == 0 && ts.tv_nsec == 999999999L);
    /* exactly the value the old code passed to nanosleep() for "-c 1000000", which is EINVAL */
    ts = ns_to_timespec(1000000000LL);
    CHECK(ts.tv_sec == 1 && ts.tv_nsec == 0);
    ts = ns_to_timespec(2500000007LL);
    CHECK(ts.tv_sec == 2 && ts.tv_nsec == 500000007L);
    ts = ns_to_timespec(INT64_MAX);
    CHECK(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L && ts.tv_sec > 0);
}

static void test_sleep_and_clock(void) {
    int64_t t0, t1;
    CHECK(sleep_ns(0) == 0);
    CHECK(sleep_ns(-100) == 0);
    t0 = now_ns();
    CHECK(t0 > 0);
    CHECK(sleep_ns(5000000) == 0); /* 5 ms */
    t1 = now_ns();
    CHECK(t1 - t0 >= 5000000);
    CHECK(t1 >= t0);
}

int main(void) {
    test_parse_long_ok();
    test_parse_long_rejects();
    test_parse_double();
    test_ns_to_timespec();
    test_sleep_and_clock();
    return 0;
}
