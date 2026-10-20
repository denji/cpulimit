/**
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012, by:  Angelo Marletta <angelo dot marletta at gmail dot com>
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

/*
 * Allocation seam. Production code uses the libc allocator; the unit tests
 * compile the modules that allocate with -DCL_ALLOC_HEADER='"fault_alloc.h"'
 * to inject allocation failures and to count leaks deterministically.
 */
#ifndef CPULIMIT_ALLOC_H
#define CPULIMIT_ALLOC_H

#ifdef CL_ALLOC_HEADER
// cppcheck-suppress preprocessorErrorDirective
#include CL_ALLOC_HEADER
#else
#include <stdlib.h>
#define CL_MALLOC(n) malloc(n)
#define CL_REALLOC(p, n) realloc((p), (n))
#define CL_FREE(p) free(p)
#endif

#endif
