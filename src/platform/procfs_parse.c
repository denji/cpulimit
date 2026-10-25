/**
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012 Angelo Marletta
 * Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <string.h>

#include "platform/procfs_parse.h"

/* field numbers as documented in proc(5) */
#define FIELD_STATE 3
#define FIELD_PPID 4
#define FIELD_UTIME 14
#define FIELD_STIME 15
#define FIELD_STARTTIME 22

/* plain decimal digits only; reject empty input and anything that does not fit in 64 bits */
static int parse_digits(const char *s, size_t len, uint64_t *out) {
    uint64_t v = 0;
    size_t i;
    if (len == 0) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        unsigned d;
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        d = (unsigned)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10) {
            return -1;
        }
        v = v * 10 + d;
    }
    *out = v;
    return 0;
}

int parse_proc_stat(const char *buf, struct proc_stat *out) {
    const char *p;
    struct proc_stat r;
    int field;
    if (buf == NULL) {
        return -1;
    }
    p = strrchr(buf, ')');
    if (p == NULL) {
        return -1;
    }
    p++;
    memset(&r, 0, sizeof(r));
    for (field = FIELD_STATE; field <= FIELD_STARTTIME; field++) {
        const char *start;
        size_t len;
        uint64_t v;
        while (*p == ' ') {
            p++;
        }
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '\n') {
            p++;
        }
        len = (size_t)(p - start);
        if (len == 0) {
            return -1; /* fewer fields than expected */
        }
        switch (field) {
        case FIELD_STATE:
            if (len != 1) {
                return -1;
            }
            r.state = *start;
            break;
        case FIELD_PPID:
            if (parse_digits(start, len, &v) != 0 || v > 2147483647u) {
                return -1;
            }
            r.ppid = (pid_t)v;
            break;
        case FIELD_UTIME:
            if (parse_digits(start, len, &r.utime) != 0) {
                return -1;
            }
            break;
        case FIELD_STIME:
            if (parse_digits(start, len, &r.stime) != 0) {
                return -1;
            }
            break;
        case FIELD_STARTTIME:
            if (parse_digits(start, len, &r.starttime) != 0) {
                return -1;
            }
            break;
        default:
            break; /* fields we do not use may hold anything, including negative numbers */
        }
    }
    *out = r;
    return 0;
}

/* seconds * 1000 is at most about 9.2e18 here and the fraction adds < 1000, which can still step
 * past INT64_MAX (found by verif/verify_ticks.c: ticks = 2^63 + 1, hz = 1000), so clamp the sum. */
static int64_t clamp_ms(uint64_t ms) {
    return ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)ms;
}

int64_t ticks_to_ms(uint64_t ticks, long clk_tck) {
    uint64_t hz, seconds, rest;
    if (clk_tck <= 0) {
        return 0;
    }
    hz = (uint64_t)clk_tck;
    seconds = ticks / hz;
    rest = ticks % hz;
    if (seconds > (uint64_t)INT64_MAX / 1000u) {
        return INT64_MAX;
    }
    if (rest <= UINT64_MAX / 1000u) {
        return clamp_ms(seconds * 1000u + rest * 1000u / hz);
    }
    {
        /* Only for an absurd clock rate (hz > 1.8e16): rest * 1000 does not fit in 64 bits, so
         * divide by repeated addition. rest < hz <= LONG_MAX, hence r + rest < 2^64 always. */
        uint64_t r = 0, q = 0;
        int i;
        for (i = 0; i < 1000; i++) {
            r += rest;
            if (r >= hz) {
                r -= hz;
                q++;
            }
        }
        return clamp_ms(seconds * 1000u + q);
    }
}
