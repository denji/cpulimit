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
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/vfs.h>

#include "core/alloc.h"
#include "platform/procfs_parse.h"
#include "platform/snapshot.h"

/*
 * Linux iterator. Three ways to walk the processes, chosen once in init:
 *   IT_SINGLE    a single pid, no children:  one read of /proc/<pid>/stat, no directory scan
 *   IT_SCAN      every process (pid == 0):   stream /proc, one stat read per entry
 *   IT_COLLECT   a pid and its descendants:  one stat read per process in /proc, then the
 *                descendants are found in memory (the old code re-read the parent chain of
 *                every process on the system), yielded from a snapshot (IT_SNAPSHOT)
 * /proc/<pid>/cmdline is read only when filter->need_command is set.
 * Zombies are not reported: they cannot use CPU and the old code dropped them as well
 * (their cmdline is empty).
 */
enum { IT_DONE = 0, IT_SINGLE, IT_SCAN, IT_COLLECT, IT_SNAPSHOT };

#define STAT_BUF_SIZE 1024

static int check_proc(void) {
    struct statfs mnt;
    if (statfs("/proc", &mnt) < 0) {
        return 0;
    }
    if (mnt.f_type != 0x9fa0) {  //PROC_SUPER_MAGIC
        return 0;
    }
    return 1;
}

static long clock_ticks(void) {
    static long hz = 0;
    if (hz <= 0) {
        hz = sysconf(_SC_CLK_TCK);
        if (hz <= 0) {
            hz = 100;
        }
    }
    return hz;
}

//read at most size-1 bytes of a small procfs file in a single read; NUL-terminate
//return the byte count (0 for an empty file) or -1 on error
static ssize_t read_small_file(const char *path, char *buf, size_t size) {
    ssize_t n;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    do {
        n = read(fd, buf, size - 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    if (n < 0) {
        return -1;
    }
    buf[n] = '\0';
    return n;
}

//a directory entry that is a pid (digits only, 1..INT_MAX), or 0
static pid_t parse_pid_name(const char *name) {
    long v = 0;
    const char *c;
    if (name[0] == '\0') {
        return 0;
    }
    for (c = name; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            return 0;
        }
        v = v * 10 + (*c - '0');
        if (v > INT32_MAX) {
            return 0;
        }
    }
    return (pid_t)v;
}

//stat of a live (non-zombie) process; 0 on success, -1 if it is gone, unreadable or a zombie
static int stat_of(pid_t pid, struct snap_rec *r) {
    char path[48];
    char buf[STAT_BUF_SIZE];
    struct proc_stat st;
    int64_t cpu;
    ssize_t n;
    long hz = clock_ticks();
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    n = read_small_file(path, buf, sizeof(buf));
    if (n <= 0 || parse_proc_stat(buf, &st) != 0) {
        return -1;
    }
    if (st.state == 'Z' || st.state == 'X' || st.state == 'x') {
        return -1;
    }
    cpu = ticks_to_ms(st.utime, hz);
    if (cpu > INT64_MAX - ticks_to_ms(st.stime, hz)) {
        cpu = INT64_MAX;
    } else {
        cpu += ticks_to_ms(st.stime, hz);
    }
    r->pid = pid;
    r->ppid = st.ppid;
    r->cputime = cpu;
    r->starttime = st.starttime > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)st.starttime;
    r->selected = 0;
    return 0;
}

//argv[0] of the process, or "" if it cannot be read or is empty
static void read_command(pid_t pid, char *dst) {
    char path[48];
    char buf[PATH_MAX + 1];
    ssize_t n;
    size_t len;
    dst[0] = '\0';
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    n = read_small_file(path, buf, sizeof(buf));
    if (n <= 0) {
        return;
    }
    len = strnlen(buf, (size_t)n);  //the first NUL-terminated string is argv[0]
    memcpy(dst, buf, len);
    dst[len] = '\0';
}

static void fill_process(struct process *p, const struct snap_rec *r, int need_command) {
    p->pid = r->pid;
    p->ppid = r->ppid;
    p->cputime = r->cputime;
    p->starttime = r->starttime;
    p->cpu_usage = -1;
    p->group_seq = 0;
    p->command[0] = '\0';
    if (need_command) {
        read_command(r->pid, p->command);
    }
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
    if (!check_proc()) {
        fprintf(stderr, "procfs is not mounted!\nAborting\n");
        exit(-2);
    }
    memset(it, 0, sizeof(*it));
    it->filter = filter;
    if (filter->pid != 0 && !filter->include_children) {
        it->mode = IT_SINGLE;
        return 0;
    }
    //open a directory stream to /proc directory
    it->dip = opendir("/proc");
    if (it->dip == NULL) {
        perror("opendir");
        it->mode = IT_DONE;
        return -1;
    }
    it->mode = filter->pid == 0 ? IT_SCAN : IT_COLLECT;
    return 0;
}

static int collect_group(struct process_iterator *it) {
    struct dirent *dit;
    size_t cap = 0;
    while ((dit = readdir(it->dip)) != NULL) {
        struct snap_rec r;
        pid_t pid = parse_pid_name(dit->d_name);
        if (pid == 0 || stat_of(pid, &r) != 0) {
            continue;  //not a process, already gone, or a zombie
        }
        if (it->nrecs == cap) {
            size_t ncap = cap == 0 ? 256 : cap * 2;
            struct snap_rec *grown = (struct snap_rec *)CL_REALLOC(it->recs, ncap * sizeof(*grown));
            if (grown == NULL) {
                return -1;
            }
            it->recs = grown;
            cap = ncap;
        }
        it->recs[it->nrecs++] = r;
    }
    closedir(it->dip);
    it->dip = NULL;
    return snapshot_select(it->recs, it->nrecs, sizeof(*it->recs), it->filter->pid, 1);
}

int get_next_process(struct process_iterator *it, struct process *p) {
    struct dirent *dit;
    struct snap_rec r;
    int need = it->filter != NULL && it->filter->need_command;
    switch (it->mode) {
    case IT_SINGLE:
        it->mode = IT_DONE;
        if (stat_of(it->filter->pid, &r) != 0) {
            return -1;
        }
        fill_process(p, &r, need);
        return 0;
    case IT_SCAN:
        //read in from /proc and seek for process dirs
        while ((dit = readdir(it->dip)) != NULL) {
            pid_t pid = parse_pid_name(dit->d_name);
            if (pid != 0 && stat_of(pid, &r) == 0) {
                fill_process(p, &r, need);
                return 0;
            }
        }
        //end of processes
        closedir(it->dip);
        it->dip = NULL;
        it->mode = IT_DONE;
        return -1;
    case IT_COLLECT:
        if (collect_group(it) != 0) {
            it->mode = IT_DONE;
            return -1;
        }
        it->mode = IT_SNAPSHOT;
    /* fall through */
    case IT_SNAPSHOT:
        while (it->pos < it->nrecs) {
            const struct snap_rec *rec = &it->recs[it->pos++];
            if (rec->selected) {
                fill_process(p, rec, need);
                return 0;
            }
        }
        it->mode = IT_DONE;
        return -1;
    default:
        return -1;
    }
}

int close_process_iterator(struct process_iterator *it) {
    int ret = 0;
    if (it->dip != NULL && closedir(it->dip) == -1) {
        perror("closedir");
        ret = 1;
    }
    it->dip = NULL;
    CL_FREE(it->recs);
    it->recs = NULL;
    it->nrecs = 0;
    it->pos = 0;
    it->mode = IT_DONE;
    return ret;
}
