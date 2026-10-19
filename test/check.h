/* Minimal assertion macro for the unit tests. Unlike assert() it does not
 * depend on NDEBUG, so a test can never silently turn into a no-op. */
#ifndef CPULIMIT_TEST_CHECK_H
#define CPULIMIT_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

#endif
