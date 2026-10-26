/**
 *
 * cpulimit - limit the CPU usage of a process
 *
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
/* OpenBSD process iterator: the shared sysctl implementation in platform/bsd_sysctl.h.
 *
 * Facts it relies on, from the OpenBSD sources (sys/kern/kern_sysctl.c, sys/sys/sysctl.h) and
 * from a probe run in CI (test/support/iterprobe.c, OpenBSD 7.9 and 8.0, hz = stathz = 100):
 *  - KERN_PROC_ALL returns every process except kernel threads, zombies included.
 *  - A zombie has p_stat == SDEAD and its tick counters are 0.
 *  - p_rtime is only brought up to date when a process is switched out: for a busy process it
 *    advanced in 200 ms steps. p_uticks, p_sticks and p_iticks (statistics clock hits) advance
 *    continuously, about 6 per 50 ms, so they are the CPU time source.
 *  - KERN_PROC_ARGV returns cnt + 1 pointers (the last NULL) followed by the strings.
 * The unit tests (process table, name matching through KERN_PROC_ARGV, zombies, descendants) and
 * the command-mode tests pass on OpenBSD 7.9 in CI; on 8.0 the only failure was the timing bound
 * of one test (see test/unit/process_iterator_test.c). */

#include "platform/bsd_sysctl.h"
