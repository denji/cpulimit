/**
 *
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

#ifndef CPULIMIT_PROCESS_GROUP_H

#define CPULIMIT_PROCESS_GROUP_H

#include <stdint.h>

#include "process_iterator.h"

#include "list.h"

#define PIDHASH_SZ 1024
//hash of a pid (computed on the unsigned value: shifting a negative int is implementation-defined)
#define pid_hashfn(x) (((((unsigned)(x)) >> 8) ^ ((unsigned)(x))) & (PIDHASH_SZ - 1))

struct process_group {
    //hashtable with all the processes (array of struct list of struct process); owns the processes
    struct list *proctable[PIDHASH_SZ];
    //the processes seen by the last update (references into proctable)
    struct list *proclist;
    pid_t target_pid;
    int include_children;
    //number of the current update, used to detect processes that disappeared
    uint32_t generation;
    //monotonic time of the last sample, in nanoseconds
    int64_t last_update_ns;
};

/*
 * A process is identified by its pid AND its start time, so a new process that
 * reuses the pid of a dead one is never mistaken for it.
 */

//return 0, or -1 if memory is exhausted (the group is then closed)
int init_process_group(struct process_group *pgroup, int target_pid, int include_children);

//refresh the group and the CPU usage of its processes
//return 0, or -1 if memory is exhausted: proclist is then incomplete and must not be used
//for this cycle, but the group stays valid and can still be updated or closed
int update_process_group(struct process_group *pgroup);

//same as update_process_group() with an explicit monotonic time in nanoseconds (test seam)
int update_process_group_at(struct process_group *pgroup, int64_t now_ns);

//free everything; safe to call twice
int close_process_group(struct process_group *pgroup);

//pid of the process: positive if it exists and can be signalled,
//0 if there is no such process, negative pid if it exists but cannot be controlled
int find_process_by_pid(pid_t pid);

//pid of the first process whose command matches (see process_group.c), 0 if none,
//negative pid if one matches but cannot be controlled
int find_process_by_name(const char *process_name);

//forget (and free) a process; also drops it from proclist
//return 0 if removed, 1 if nothing is stored for that hash, 2 if the pid is unknown
int remove_process(struct process_group *pgroup, int pid);

#endif
