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
/* NetBSD process iterator: the shared sysctl implementation in platform/bsd_sysctl.h, over
 * KERN_PROC2 and struct kinfo_proc2.
 *
 * STATUS: written from the NetBSD sources (sys/sys/sysctl.h, sys/kern/kern_proc.c) without a
 * NetBSD host; only the NetBSD job of the vm workflow has run it. Read from the sources:
 *  - KERN_PROC2 takes {op, arg, elem_size, elem_count}; the size query adds slack; zombies are
 *    listed; the only system process is pid 0.
 *  - KERN_PROC_ARGV is the packed, NUL-terminated argument strings; zombies and system
 *    processes answer EINVAL.
 * Not known from the sources, so assumed the same as on OpenBSD until the probe says otherwise:
 * that p_rtime lags for a running process (the tick counters are used), the value of p_stat for a
 * zombie (SZOMB = 5 is assumed) and the statistics clock rate (read from kern.clockrate). */

#include "platform/bsd_sysctl.h"
