/**
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sun__
    #include <procfs.h>
#elif !defined __APPLE__ && !defined __OpenBSD__
    #include <unistd.h>
    #include <sys/procfs.h>
#endif
#include <time.h>
#include "platform/iterator.h"

//See this link to port to other systems: http://www.steve.org.uk/Reference/Unix/faq_8.html#SEC85

#ifdef __linux__

    #include "linux/iterator.c"

#elif defined __FreeBSD__

    #include "freebsd/iterator.c"

#elif defined __APPLE__

    #include "macos/iterator.c"

#elif defined __OpenBSD__

    #include "openbsd/iterator.c"

#elif defined __sun__

    #include "solaris/iterator.c"

#else

    #error Platform not supported

#endif
