/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* cli_parse(): every rule of the command line, in-process (no fork, no exit). */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "support/check.h"
#include "cli/cli.h"

#define NCPU 4
#define PID_MAX 4194304L

static enum cli_result run_with(int ncpu, long pid_max, struct cli_config *cfg, char *msg, size_t msgsize, ...) {
    char *argv[64];
    int argc = 0;
    va_list ap;
    const char *a;
    FILE *err = tmpfile();
    enum cli_result r;
    size_t n;
    CHECK(err != NULL);
    va_start(ap, msgsize);
    while ((a = va_arg(ap, const char *)) != NULL) {
        CHECK(argc < 63);
        argv[argc++] = (char *)a;
    }
    va_end(ap);
    argv[argc] = NULL;
    r = cli_parse(argc, argv, ncpu, pid_max, cfg, err);
    rewind(err);
    n = fread(msg, 1, msgsize - 1, err);
    msg[n] = '\0';
    fclose(err);
    return r;
}

#define RUN(cfg, msg, ...) run_with(NCPU, PID_MAX, &(cfg), (msg), sizeof(msg), __VA_ARGS__, NULL)

static void expect_error(const char *what, const char *needle, enum cli_result r, const char *msg) {
    if (r != CLI_USAGE || strstr(msg, needle) == NULL) {
        fprintf(stderr, "case '%s': result %d, message '%s', wanted CLI_USAGE with '%s'\n", what, (int)r, msg, needle);
        exit(1);
    }
}

static void test_valid_forms(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "50", "-p", "123") == CLI_OK);
    CHECK(c.limit_pct == 50 && c.pid == 123 && c.pid_set && c.lazy); /* -p implies -z */
    CHECK(c.chunk_us == CL_DEFAULT_CHUNK_US && !c.exe_set && c.command_start == 0);
    CHECK(m[0] == '\0'); /* nothing printed when fine */

    CHECK(RUN(c, m, "cpulimit", "-l", "10", "-e", "nginx") == CLI_OK);
    CHECK(c.exe_set && strcmp(c.exe, "nginx") == 0 && !c.lazy && !c.pid_set);

    CHECK(RUN(c, m, "cpulimit", "-l", "10", "sleep", "5") == CLI_OK);
    CHECK(c.command_start == 3 && c.lazy);

    /* options after the command word belong to the command, not to cpulimit */
    CHECK(RUN(c, m, "cpulimit", "-l", "10", "ls", "-l", "-p", "7") == CLI_OK);
    CHECK(c.command_start == 3 && !c.pid_set && c.limit_pct == 10);

    CHECK(RUN(c, m, "cpulimit", "--limit=75", "--pid=99", "--chunk-size=2000", "--verbose", "--include-children",
              "--minimum-limited-cpu=0.25", "--lazy") == CLI_OK);
    CHECK(c.limit_pct == 75 && c.pid == 99 && c.chunk_us == 2000 && c.verbose && c.include_children);
    CHECK(c.min_cpu == 0.25 && c.lazy);

    CHECK(RUN(c, m, "cpulimit", "-l", "10", "-p", "5", "-d", "-s", "-P", "/tmp/x.pid", "--no-lock", "--no-boost") ==
          CLI_OK);
    CHECK(c.daemonize && c.use_syslog && strcmp(c.pidfile, "/tmp/x.pid") == 0 && c.no_lock && c.no_boost);
}

static void test_help_and_version(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-V") == CLI_VERSION);
    CHECK(RUN(c, m, "cpulimit", "--version") == CLI_VERSION);
    CHECK(RUN(c, m, "cpulimit", "-h") == CLI_HELP);
    CHECK(RUN(c, m, "cpulimit", "--help") == CLI_HELP);
    CHECK(RUN(c, m, "cpulimit", "-l", "abc", "-h") == CLI_HELP); /* help wins over later validation, as it always did */
    CHECK(strcmp(cli_version(), "1.0.3") == 0);
}

static void test_limit(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "0", "-p", "5") == CLI_OK && c.limit_pct == 0);
    CHECK(RUN(c, m, "cpulimit", "-l", "400", "-p", "5") == CLI_OK && c.limit_pct == 400); /* 100 * ncpu */
    expect_error("limit above ncpu", "limit must be in the range 0-400", RUN(c, m, "cpulimit", "-l", "401", "-p", "5"),
                 m);
    expect_error("limit negative", "limit must be in the range", RUN(c, m, "cpulimit", "-l", "-1", "-p", "5"), m);
    expect_error("limit text", "limit must be in the range", RUN(c, m, "cpulimit", "-l", "abc", "-p", "5"), m);
    expect_error("limit trailing", "limit must be in the range", RUN(c, m, "cpulimit", "-l", "50x", "-p", "5"), m);
    expect_error("limit empty", "limit must be in the range", RUN(c, m, "cpulimit", "-l", "", "-p", "5"), m);
    expect_error("limit float", "limit must be in the range", RUN(c, m, "cpulimit", "-l", "5.5", "-p", "5"), m);
    expect_error("limit huge", "limit must be in the range",
                 RUN(c, m, "cpulimit", "-l", "99999999999999999999", "-p", "5"), m);
    expect_error("limit missing", "You must specify a cpu limit percentage", RUN(c, m, "cpulimit", "-p", "5"), m);
    CHECK(run_with(0, PID_MAX, &c, m, sizeof(m), "cpulimit", "-l", "100", "-p", "5", NULL) ==
          CLI_OK); /* unknown ncpu counts as 1 */
    CHECK(run_with(-3, PID_MAX, &c, m, sizeof(m), "cpulimit", "-l", "101", "-p", "5", NULL) == CLI_USAGE);
}

static void test_chunk(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "1") == CLI_OK && c.chunk_us == 1);
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "1000000") == CLI_OK && c.chunk_us == 1000000);
    expect_error("chunk 0", "chunk size must be in the range 1-1000000",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "0"), m);
    expect_error("chunk too big", "chunk size must be in the range",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "1000001"), m);
    expect_error("chunk text", "chunk size must be in the range",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "abc"), m);
    expect_error("chunk negative", "chunk size must be in the range",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-c", "-5"), m);
}

static void test_pid(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "2") == CLI_OK);
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "4194303") == CLI_OK);
    expect_error("pid 1", "Invalid value for argument PID", RUN(c, m, "cpulimit", "-l", "5", "-p", "1"), m);
    expect_error("pid 0", "Invalid value for argument PID", RUN(c, m, "cpulimit", "-l", "5", "-p", "0"), m);
    expect_error("pid negative", "Invalid value for argument PID", RUN(c, m, "cpulimit", "-l", "5", "-p", "-7"), m);
    expect_error("pid text", "Invalid value for argument PID", RUN(c, m, "cpulimit", "-l", "5", "-p", "12x"), m);
    expect_error("pid at the limit", "Invalid value for argument PID",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "4194304"), m);
    /* when the kernel limit cannot be read, any pid above 1 is acceptable (it used to reject ALL pids) */
    CHECK(run_with(NCPU, -1, &c, m, sizeof(m), "cpulimit", "-l", "5", "-p", "4194304", NULL) == CLI_OK);
    CHECK(run_with(NCPU, 0, &c, m, sizeof(m), "cpulimit", "-l", "5", "-p", "5", NULL) == CLI_OK);
}

static void test_minimum_cpu(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "0") == CLI_OK && c.min_cpu == 0);
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "1e-3") == CLI_OK && c.min_cpu == 1e-3);
    expect_error("-m text", "minimum-limited-cpu", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "abc"), m);
    expect_error("-m negative", "minimum-limited-cpu", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "-1"), m);
    expect_error("-m nan", "minimum-limited-cpu", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "nan"), m);
    expect_error("-m inf", "minimum-limited-cpu", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "inf"), m);
    expect_error("-m overflow", "minimum-limited-cpu", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m", "1e999"), m);
    /* the argument of the long form is mandatory: this used to crash */
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "--minimum-limited-cpu") == CLI_USAGE);
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-m") == CLI_USAGE);
}

static void test_targets(void) {
    struct cli_config c;
    char m[512];
    expect_error("no target", "You must specify one target process", RUN(c, m, "cpulimit", "-l", "5"), m);
    expect_error("pid and exe", "exactly one target process", RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-e", "x"),
                 m);
    expect_error("pid and command", "exactly one target process",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "sleep", "1"), m);
    expect_error("exe and command", "exactly one target process",
                 RUN(c, m, "cpulimit", "-l", "5", "-e", "x", "sleep", "1"), m);
    expect_error("empty exe", "executable name must not be empty", RUN(c, m, "cpulimit", "-l", "5", "-e", ""), m);
}

static void test_daemon_rules(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-l", "5", "-e", "x", "--daemon", "--pidfile=/run/x.pid") == CLI_OK);
    expect_error("daemon + command", "--daemon cannot run a command",
                 RUN(c, m, "cpulimit", "-l", "5", "-d", "sleep", "1"), m);
    expect_error("pidfile alone", "--pidfile requires --daemon",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-P", "/x"), m);
    expect_error("empty pidfile", "pid file name must not be empty",
                 RUN(c, m, "cpulimit", "-l", "5", "-p", "5", "-d", "-P", ""), m);
}

static void test_unknown_options(void) {
    struct cli_config c;
    char m[512];
    CHECK(RUN(c, m, "cpulimit", "-x", "-l", "5", "-p", "5") == CLI_USAGE);
    CHECK(RUN(c, m, "cpulimit", "--bogus", "-l", "5", "-p", "5") == CLI_USAGE);
    CHECK(RUN(c, m, "cpulimit", "-l") == CLI_USAGE); /* missing argument */
    CHECK(strstr(m, "cpulimit") != NULL);            /* the message names the program */
    CHECK(RUN(c, m, "cpulimit") == CLI_USAGE);
}

/* parsing twice must give the same answer (getopt keeps global state) */
static void test_repeatable(void) {
    struct cli_config a, b;
    char m[512];
    int i;
    for (i = 0; i < 3; i++) {
        CHECK(RUN(a, m, "cpulimit", "-l", "30", "-i", "-e", "foo") == CLI_OK);
        CHECK(RUN(b, m, "cpulimit", "-l", "60", "sleep", "9") == CLI_OK);
        CHECK(a.limit_pct == 30 && a.include_children && a.exe_set && a.command_start == 0);
        CHECK(b.limit_pct == 60 && !b.include_children && !b.exe_set && b.command_start == 3);
    }
}

/* ---- seeded random argument vectors: no crash, and what is accepted obeys the rules ---- */
static uint32_t rng = 0x12345678u;
TEST_PRNG static uint32_t next_random(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

static void test_random_argv(void) {
    static const char *const pool[] = { "-l",         "-p",
                                        "-e",         "-c",
                                        "-m",         "-v",
                                        "-V",         "-z",
                                        "-i",         "-d",
                                        "-s",         "-P",
                                        "-h",         "--limit",
                                        "--pid",      "--no-lock",
                                        "--no-boost", "--daemon",
                                        "--pidfile",  "--limit=50",
                                        "--pid=77",   "--chunk-size=0",
                                        "0",          "1",
                                        "5",          "50",
                                        "400",        "401",
                                        "-1",         "99999999999999999999",
                                        "1e3",        "nan",
                                        "abc",        "",
                                        "-",          "--",
                                        "sleep",      "x",
                                        "/bin/ls",    "4194304",
                                        "0.5",        "\x01",
                                        "--x",        "-lp",
                                        "-l50",       "-p7" };
    int iter;
    long accepted = 0;
    for (iter = 0; iter < 30000; iter++) {
        struct cli_config c;
        char msg[512];
        char *argv[16];
        int argc = 1, n = (int)(next_random() % 9), i;
        enum cli_result r;
        FILE *err = tmpfile();
        CHECK(err != NULL);
        argv[0] = (char *)"cpulimit";
        for (i = 0; i < n; i++) {
            argv[argc++] = (char *)pool[next_random() % (sizeof(pool) / sizeof(pool[0]))];
        }
        argv[argc] = NULL;
        r = cli_parse(argc, argv, NCPU, PID_MAX, &c, err);
        fclose(err);
        (void)msg;
        if (r == CLI_OK) {
            int targets = c.pid_set + c.exe_set + (c.command_start != 0);
            accepted++;
            CHECK(targets == 1);
            CHECK(c.limit_pct >= 0 && c.limit_pct <= 100 * NCPU);
            CHECK(c.chunk_us >= 1 && c.chunk_us <= CL_MAX_CHUNK_US);
            CHECK(!c.pid_set || (c.pid > 1 && c.pid < PID_MAX));
            CHECK(c.min_cpu >= 0 && c.min_cpu == c.min_cpu);
            CHECK(!c.daemonize || c.command_start == 0);
            CHECK(c.pidfile == NULL || c.daemonize);
            CHECK(!c.exe_set || c.exe[0] != '\0');
            CHECK(c.command_start == 0 || (c.command_start > 0 && c.command_start < argc));
        }
    }
    CHECK(accepted > 100); /* the generator does reach valid command lines, so the checks above ran */
}

int main(void) {
    test_valid_forms();
    test_help_and_version();
    test_limit();
    test_chunk();
    test_pid();
    test_minimum_cpu();
    test_targets();
    test_daemon_rules();
    test_unknown_options();
    test_repeatable();
    test_random_argv();
    return 0;
}
