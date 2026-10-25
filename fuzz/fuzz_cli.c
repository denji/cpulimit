/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* Fuzz target for cli_parse(). The input is split at NUL bytes into an argv (at most 32 words).
 *
 * Properties checked on every successful parse (the validated configuration is what the rest of
 * the program trusts, so these are the preconditions it relies on):
 *   C1  exactly one target is selected: -p, -e or a command
 *   C2  0 <= limit_pct <= 100 * ncpu, 1 <= chunk_us <= CL_MAX_CHUNK_US,
 *       min_cpu a finite number >= 0 (no upper bound: -m is a threshold, and every release accepted any value)
 *   C3  command_start, when set, indexes a word inside argv
 *   C4  a pid target is positive, and below pid_max when that is known
 *   C5  a daemon never takes a command, -P implies -d
 * cli_parse() must also be repeatable: the same argv twice gives the same answer. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/cli.h"

#define FAIL(msg)                                                                                                      \
    do {                                                                                                               \
        fprintf(stderr, "fuzz_cli: %s (line %d)\n", msg, __LINE__);                                                    \
        abort();                                                                                                       \
    } while (0)
#define MAX_WORDS 32

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static FILE *sink;
    char *buf;
    char *argv[MAX_WORDS + 2];
    int argc = 0;
    size_t i, start = 0;
    int ncpu = 1 + (size ? data[0] % 8 : 0); /* size >= 2 is checked below */
    long pid_max = size > 1 ? (data[1] % 3 == 0 ? -1 : 32768L << (data[1] % 8)) : 4194304;
    struct cli_config c1, c2;
    enum cli_result r1, r2;

    if (sink == NULL) {
        sink = fopen("/dev/null", "w");
        if (sink == NULL) {
            abort();
        }
    }
    /* the first two bytes only choose ncpu and pid_max; the rest is the argument words */
    if (size < 2) {
        return 0;
    }
    data += 2;
    size -= 2;
    buf = malloc(size + 1);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    argv[argc++] = (char *)"cpulimit";
    for (i = 0; i <= size && argc <= MAX_WORDS; i++) {
        if (buf[i] == '\0') {
            argv[argc++] = buf + start;
            start = i + 1;
        }
    }
    argv[argc] = NULL;

    memset(&c1, 0, sizeof(c1));
    memset(&c2, 0, sizeof(c2));
    r1 = cli_parse(argc, argv, ncpu, pid_max, &c1, sink);
    r2 = cli_parse(argc, argv, ncpu, pid_max, &c2, sink);
    if (r1 != r2 || (r1 == CLI_OK && memcmp(&c1, &c2, sizeof(c1)) != 0)) {
        FAIL("cli_parse is not repeatable");
    }
    if (r1 == CLI_OK) {
        int targets = c1.pid_set + c1.exe_set + (c1.command_start != 0);
        if (targets != 1) {
            FAIL("C1 not exactly one target");
        }
        if (c1.limit_pct < 0 || c1.limit_pct > 100 * ncpu || c1.chunk_us < 1 || c1.chunk_us > CL_MAX_CHUNK_US ||
            !(c1.min_cpu >= 0.0) || !isfinite(c1.min_cpu)) {
            FAIL("C2 value out of range");
        }
        if (c1.command_start != 0 && (c1.command_start < 1 || c1.command_start >= argc)) {
            FAIL("C3 command_start outside argv");
        }
        if (c1.pid_set && (c1.pid <= 0 || (pid_max > 0 && c1.pid > pid_max))) {
            FAIL("C4 pid out of range");
        }
        if ((c1.daemonize && c1.command_start != 0) || (c1.pidfile != NULL && !c1.daemonize)) {
            FAIL("C5 daemon option combination");
        }
    }
    free(buf);
    return 0;
}
