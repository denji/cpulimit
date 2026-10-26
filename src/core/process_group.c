/**
 *
 * cpulimit - limit the CPU usage of a process
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

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "core/alloc.h"
#include "platform/iterator.h"
#include "core/process_group.h"
#include "core/list.h"
#include "util/util.h"

// look for a process by pid
// pid : pid of the wanted process
// return:  pid of the process, if it exists and can be signalled
//          0, if there is no such process
//          negative pid, if it exists but the signal is not permitted
int find_process_by_pid(pid_t pid) {
    if (pid <= 0) {
        return 0;
    }
    if (kill(pid, 0) == 0) {
        return pid;
    }
    return errno == ESRCH ? 0 : -pid;
}

// look for a process with a given name
// process_name: the name of the wanted process. A name without '/' is compared with the
//         file name of the command (prefix match, as it always was); a name with a '/' is
//         compared with the whole command path (the documented "absolute path name").
// return:  pid of the found process, if it is found
//         0, if it's not found
//         negative pid, if it is found but it's not possible to control it
int find_process_by_name(const char *process_name) {
    pid_t found = 0;
    pid_t denied = 0;
    size_t len;
    int by_path;
    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    if (process_name == NULL || process_name[0] == '\0') {
        return 0;  //an empty name would match the first process of the system
    }
    len = strlen(process_name);
    by_path = strchr(process_name, '/') != NULL;
    memset(&filter, 0, sizeof(filter));
    filter.need_command = 1;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1) {
        const char *command = by_path ? proc.command : base_name(proc.command);
        if (proc.pid == getpid()) {
            continue;  //never match ourselves: "-e cpu" would otherwise find cpulimit itself
        }
        if (strncmp(command, process_name, len) != 0) {
            continue;
        }
        //signal 0 only checks that the process can be controlled; it has no side effect
        if (kill(proc.pid, 0) == 0) {
            found = proc.pid;
            break;
        }
        if (errno == EPERM && denied == 0) {
            denied = proc.pid;
        }
    }
    if (close_process_iterator(&it) != 0) {
        exit(1);
    }
    if (found != 0) {
        return found;
    }
    return denied != 0 ? -denied : 0;
}

int init_process_group(struct process_group *pgroup, int target_pid, int include_children) {
    //hashtable initialization
    memset(&pgroup->proctable, 0, sizeof(pgroup->proctable));
    pgroup->target_pid = target_pid;
    pgroup->include_children = include_children;
    pgroup->generation = 0;
    pgroup->last_update_ns = 0;
    pgroup->proclist = (struct list *)CL_MALLOC(sizeof(struct list));
    if (pgroup->proclist == NULL) {
        return -1;
    }
    init_list(pgroup->proclist, 4);
    if (update_process_group(pgroup) != 0) {
        close_process_group(pgroup);
        return -1;
    }
    return 0;
}

int close_process_group(struct process_group *pgroup) {
    int i;
    int size = sizeof(pgroup->proctable) / sizeof(struct process *);
    for (i = 0; i < size; i++) {
        if (pgroup->proctable[i] != NULL) {
            //free() history for each process
            destroy_list(pgroup->proctable[i]);
            CL_FREE(pgroup->proctable[i]);
            pgroup->proctable[i] = NULL;
        }
    }
    if (pgroup->proclist != NULL) {
        clear_list(pgroup->proclist);
        CL_FREE(pgroup->proclist);
        pgroup->proclist = NULL;
    }
    return 0;
}

//parameter in range 0-1
#define ALFA 0.08
//minimum time between two samples, in milliseconds
#define MIN_DT 20

//the stored process with this pid, or NULL
static struct process *find_stored(struct list *bucket, pid_t pid) {
    struct list_node *node;
    if (bucket == NULL) {
        return NULL;
    }
    for (node = bucket->first; node != NULL; node = node->next) {
        struct process *p = (struct process *)node->data;
        if (p->pid == pid) {
            return p;
        }
    }
    return NULL;
}

//unlink a stored process from its bucket and free it
static void drop_stored(struct process_group *pgroup, struct list *bucket, struct process *p) {
    struct list_node *node;
    for (node = pgroup->proclist->first; node != NULL; node = node->next) {
        if (node->data == p) {
            delete_node(pgroup->proclist, node);
            break;
        }
    }
    for (node = bucket->first; node != NULL; node = node->next) {
        if (node->data == p) {
            delete_node(bucket, node);
            break;
        }
    }
    CL_FREE(p);
}

//free the processes that the last update did not see: they have terminated
static void sweep_terminated(struct process_group *pgroup) {
    int i;
    for (i = 0; i < PIDHASH_SZ; i++) {
        struct list *bucket = pgroup->proctable[i];
        struct list_node *node;
        if (bucket == NULL) {
            continue;
        }
        node = bucket->first;
        while (node != NULL) {
            struct list_node *next = node->next;
            struct process *p = (struct process *)node->data;
            if (p->group_seq != pgroup->generation) {
                delete_node(bucket, node);
                CL_FREE(p);
            }
            node = next;
        }
    }
}

int update_process_group(struct process_group *pgroup) {
    return update_process_group_at(pgroup, now_ns());
}

int update_process_group_at(struct process_group *pgroup, int64_t now) {
    struct process_iterator it;
    struct process tmp_process;
    struct process_filter filter;
    int ret = 0;
    //time elapsed from previous sample (in ms)
    int64_t dt = (now - pgroup->last_update_ns) / 1000000;
    memset(&filter, 0, sizeof(filter));
    filter.pid = pgroup->target_pid;
    filter.include_children = pgroup->include_children;
    pgroup->generation++;
    init_process_iterator(&it, &filter);
    clear_list(pgroup->proclist);
    init_list(pgroup->proclist, 4);

    while (get_next_process(&it, &tmp_process) != -1) {
        int hashkey = pid_hashfn(tmp_process.pid);
        struct process *p;
        if (pgroup->proctable[hashkey] == NULL) {
            //empty bucket
            struct list *bucket = (struct list *)CL_MALLOC(sizeof(struct list));
            if (bucket == NULL) {
                ret = -1;
                break;
            }
            init_list(bucket, 4);
            pgroup->proctable[hashkey] = bucket;
        }
        p = find_stored(pgroup->proctable[hashkey], tmp_process.pid);
        if (p != NULL && p->starttime != tmp_process.starttime) {
            //same pid, different process: the old one has terminated and its pid was reused
            drop_stored(pgroup, pgroup->proctable[hashkey], p);
            p = NULL;
        }
        if (p == NULL) {
            //process is new. add it
            struct process *new_process = (struct process *)CL_MALLOC(sizeof(struct process));
            if (new_process == NULL) {
                ret = -1;
                break;
            }
            tmp_process.cpu_usage = -1;
            tmp_process.group_seq = pgroup->generation;
            memcpy(new_process, &tmp_process, sizeof(struct process));
            if (add_elem(pgroup->proctable[hashkey], new_process) == NULL) {
                CL_FREE(new_process);
                ret = -1;
                break;
            }
            if (add_elem(pgroup->proclist, new_process) == NULL) {
                //keep the table consistent: the entry is stored but not listed this cycle
                ret = -1;
                break;
            }
        } else {
            p->group_seq = pgroup->generation;
            if (add_elem(pgroup->proclist, p) == NULL) {
                ret = -1;
                break;
            }
            if (dt < MIN_DT) {
                continue;
            }
            //process exists. update CPU usage
            double sample = 1.0 * (tmp_process.cputime - p->cputime) / dt;
            if (sample < 0) {
                sample = 0;
            }
            if (p->cpu_usage == -1) {
                //initialization
                p->cpu_usage = sample;
            } else {
                //usage adjustment
                p->cpu_usage = (1.0 - ALFA) * p->cpu_usage + ALFA * sample;
            }
            p->cputime = tmp_process.cputime;
        }
    }
    close_process_iterator(&it);
    if (ret != 0) {
        return ret;
    }
    sweep_terminated(pgroup);
    if (dt < MIN_DT) {
        return 0;
    }
    pgroup->last_update_ns = now;
    return 0;
}

int remove_process(struct process_group *pgroup, int pid) {
    int hashkey = pid_hashfn(pid);
    struct process *p;
    if (pgroup->proctable[hashkey] == NULL) {
        return 1;  //nothing to delete
    }
    p = find_stored(pgroup->proctable[hashkey], (pid_t)pid);
    if (p == NULL) {
        return 2;
    }
    drop_stored(pgroup, pgroup->proctable[hashkey], p);
    return 0;
}
