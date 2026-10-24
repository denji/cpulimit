/* Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 * SPDX-License-Identifier: GPL-2.0-or-later */
/* Stand-in for libFuzzer's main() where the libFuzzer runtime is not installed.
 *
 *   driver [-n ITERATIONS] [-s SEED] FILE_OR_DIR...
 *
 * Runs every seed file once, then ITERATIONS deterministic mutations of randomly chosen
 * seeds (bit flips, byte replacement, insertion, deletion, block copy, splicing). The same
 * SEED gives the same sequence, so a failure is reproducible; the failing input is written
 * to ./crash-<seed>-<iteration> before the target's abort() fires. This is a much weaker
 * search than libFuzzer's coverage-guided one - it is here so the properties run under
 * ASan/UBSan anywhere, and CI uses real libFuzzer. */
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define MAX_LEN 512
#define MAX_SEEDS 4096

struct seed { uint8_t *data; size_t len; };
static struct seed seeds[MAX_SEEDS];
static size_t nseeds;
static uint8_t cur[MAX_LEN];
static size_t cur_len;
static char crash_name[96];

static uint64_t rng_state;
static uint64_t rnd(void) { /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

static void on_abort(int sig) {
    FILE *f = fopen(crash_name, "wb");
    (void)sig;
    if (f != NULL) {
        fwrite(cur, 1, cur_len, f);
        fclose(f);
        fprintf(stderr, "failing input saved to %s\n", crash_name);
    }
    signal(SIGABRT, SIG_DFL);
    abort();
}

static void add_file(const char *path) {
    struct stat st;
    FILE *f;
    uint8_t *d;
    size_t n;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || nseeds == MAX_SEEDS) {
        return;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    d = malloc(MAX_LEN);
    n = d ? fread(d, 1, MAX_LEN, f) : 0;
    fclose(f);
    if (d == NULL) {
        return;
    }
    seeds[nseeds].data = d;
    seeds[nseeds].len = n;
    nseeds++;
}

static void add_path(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        char full[1024];
        if (d == NULL) {
            return;
        }
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] != '.' && snprintf(full, sizeof(full), "%s/%s", path, e->d_name) < (int)sizeof(full)) {
                add_file(full);
            }
        }
        closedir(d);
    } else {
        add_file(path);
    }
}

static void run(void) { LLVMFuzzerTestOneInput(cur, cur_len); }

static void mutate(void) {
    const struct seed *s = &seeds[rnd() % nseeds];
    int rounds = 1 + (int)(rnd() % 4);
    cur_len = s->len;
    memcpy(cur, s->data, cur_len);
    while (rounds-- > 0) {
        size_t p = cur_len ? (size_t)(rnd() % cur_len) : 0;
        switch (rnd() % 7) {
        case 0: if (cur_len) cur[p] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: if (cur_len) cur[p] = (uint8_t)rnd(); break;
        case 2: /* insert */
            if (cur_len < MAX_LEN) {
                memmove(cur + p + 1, cur + p, cur_len - p);
                cur[p] = (uint8_t)rnd();
                cur_len++;
            }
            break;
        case 3: /* delete */
            if (cur_len) {
                memmove(cur + p, cur + p + 1, cur_len - p - 1);
                cur_len--;
            }
            break;
        case 4: /* interesting byte */
            if (cur_len) {
                static const uint8_t v[] = { 0, '(', ')', ' ', '\n', '-', '9', '0', 0xff, 0x7f, 0x80 };
                cur[p] = v[rnd() % sizeof(v)];
            }
            break;
        case 5: /* duplicate a block */
            if (cur_len) {
                size_t l = 1 + (size_t)(rnd() % 8), q = (size_t)(rnd() % cur_len);
                if (q + l > cur_len) l = cur_len - q;
                if (cur_len + l <= MAX_LEN) {
                    memmove(cur + q + l, cur + q, cur_len - q);
                    memcpy(cur + q, cur + q + l, l); /* the moved block, so q..q+l is a copy */
                    cur_len += l;
                }
            }
            break;
        default: { /* splice with another seed */
            const struct seed *o = &seeds[rnd() % nseeds];
            size_t keep = cur_len ? (size_t)(rnd() % (cur_len + 1)) : 0;
            size_t from = o->len ? (size_t)(rnd() % o->len) : 0;
            size_t l = o->len - from;
            if (keep + l > MAX_LEN) l = MAX_LEN - keep;
            memcpy(cur + keep, o->data + from, l);
            cur_len = keep + l;
        }
        }
    }
}

int main(int argc, char **argv) {
    unsigned long iterations = 100000, i;
    uint64_t seed = 1;
    int a;
    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-n") == 0 && a + 1 < argc) {
            iterations = strtoul(argv[++a], NULL, 10);
        } else if (strcmp(argv[a], "-s") == 0 && a + 1 < argc) {
            seed = strtoull(argv[++a], NULL, 10);
        } else {
            add_path(argv[a]);
        }
    }
    rng_state = seed * 0x9E3779B97F4A7C15ULL + 1;
    signal(SIGABRT, on_abort);
    if (nseeds == 0) {
        seeds[0].data = calloc(1, MAX_LEN);
        nseeds = seeds[0].data ? 1 : 0;
        if (nseeds == 0) return 2;
    }
    for (i = 0; i < nseeds; i++) {
        snprintf(crash_name, sizeof(crash_name), "crash-seed-%lu", i);
        cur_len = seeds[i].len;
        memcpy(cur, seeds[i].data, cur_len);
        run();
    }
    for (i = 0; i < iterations; i++) {
        snprintf(crash_name, sizeof(crash_name), "crash-%llu-%lu", (unsigned long long)seed, i);
        mutate();
        run();
    }
    printf("%zu seed(s) + %lu mutation(s), seed %llu: no property violated\n",
           nseeds, iterations, (unsigned long long)seed);
    return 0;
}
