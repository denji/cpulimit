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

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
    #include <libgen.h>
#endif
#ifdef __linux__
    #include <sys/prctl.h>
#endif

#include <process_iterator.h>
#include <process_group.h>

#include "check.h"
#include "util.h"

volatile sig_atomic_t child;

void kill_child(int sig) {
    (void)sig;
    kill(child, SIGINT);
}

static void filter_for(struct process_filter *f, pid_t pid, int include_children, int need_command) {
    memset(f, 0, sizeof(*f));
    f->pid = pid;
    f->include_children = include_children;
    f->need_command = need_command;
}

//a child that does nothing until it is killed
static pid_t spawn_idle(void) {
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        TEST_DIE_WITH_PARENT();
        for (;;) {
            pause();
        }
    }
    test_track_child(pid);
    return pid;
}

static void reap(pid_t pid) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_single_process() {
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    int count;
    //don't iterate children
    filter_for(&filter, getpid(), 0, 0);
    count = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &process) == 0) {
        CHECK(process.pid == getpid());
        CHECK(process.ppid == getppid());
        CHECK(process.cputime < 20000);
        count++;
    }
    CHECK(count == 1);
    close_process_iterator(&it);
    //iterate children (there are none)
    filter_for(&filter, getpid(), 1, 0);
    count = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &process) == 0) {
        CHECK(process.pid == getpid());
        CHECK(process.ppid == getppid());
        CHECK(process.cputime < 20000);
        count++;
    }
    CHECK(count == 1);
    close_process_iterator(&it);
}

void test_multiple_process() {
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    pid_t kid = spawn_idle();
    int count = 0;
    filter_for(&filter, getpid(), 1, 0);
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &process) == 0) {
        if (process.pid == getpid()) {
            CHECK(process.ppid == getppid());
        } else if (process.pid == kid) {
            CHECK(process.ppid == getpid());
        } else {
            CHECK(0);
        }
        CHECK(process.cputime < 20000);
        count++;
    }
    CHECK(count == 2);
    close_process_iterator(&it);
    reap(kid);
}

void test_all_processes() {
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    int count = 0, self_seen = 0;
    filter_for(&filter, 0, 0, 0);
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &process) == 0) {
        if (process.pid == getpid()) {
            CHECK(process.ppid == getppid());
            CHECK(process.cputime < 20000);
            self_seen++;
        }
        count++;
    }
    /* A container may hold only a handful of processes, so do not assume a host-like number:
     * every process is listed once, and this one is among them. */
    CHECK(self_seen == 1);
    CHECK(count >= 1);
    close_process_iterator(&it);
}

void test_process_group_all() {
    struct process_group pgroup;
    struct list_node *node = NULL;
    int count = 0;
    CHECK(init_process_group(&pgroup, 0, 0) == 0);
    CHECK(update_process_group(&pgroup) == 0);
    for (node=pgroup.proclist->first; node!= NULL; node=node->next) {
        count++;
    }
    CHECK(count >= 1);  /* see test_all_processes: no assumption about how many others exist */
    CHECK(update_process_group(&pgroup) == 0);
    CHECK(close_process_group(&pgroup) == 0);
}

//A busy child must be measured at about one CPU. CPU accounting is meaningless under
//emulation or heavy contention, so CPULIMIT_SKIP_TIMING=1 skips only the numeric check.
void test_process_group_single(int include_children) {
    struct process_group pgroup;
    int i;
    double tot_usage = 0;
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        TEST_DIE_WITH_PARENT();
        //child is supposed to be killed by the parent :/
        while (1);
    }
    test_track_child(child);
    signal(SIGABRT, &kill_child);
    signal(SIGTERM, &kill_child);
    CHECK(init_process_group(&pgroup, child, include_children) == 0);
    for (i=0; i<100; i++) {
        struct list_node *node = NULL;
        struct timespec interval;
        int count = 0;
        CHECK(update_process_group(&pgroup) == 0);
        for (node=pgroup.proclist->first; node!= NULL; node=node->next) {
            struct process *p = (struct process *)(node->data);
            CHECK(p->pid == child);
            CHECK(p->ppid == getpid());
            CHECK(p->cpu_usage <= 1.2);
            tot_usage += p->cpu_usage;
            count++;
        }
        CHECK(count == 1);
        interval.tv_sec = 0;
        interval.tv_nsec = 50000000;
        nanosleep(&interval, NULL);
    }
    if (getenv("CPULIMIT_SKIP_TIMING") == NULL) {
        CHECK(tot_usage / i < 1.1 && tot_usage / i > 0.8);
    }
    CHECK(close_process_group(&pgroup) == 0);
    reap(child);
}

void test_process_name(const char *command) {
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    filter_for(&filter, getpid(), 0, 1);
    init_process_iterator(&it, &filter);
    CHECK(get_next_process(&it, &process) == 0);
    CHECK(process.pid == getpid());
    CHECK(process.ppid == getppid());
#ifdef __APPLE__
    // proc_pidinfo only gives us the first 15 chars
    // of the basename of the command on OSX.
    CHECK(strncmp(base_name(command), base_name(process.command), 15) == 0);
#else
    CHECK(strncmp(command, process.command, strlen(process.command)) == 0);
    CHECK(process.command[0] != '\0');
#endif
    CHECK(get_next_process(&it, &process) != 0);
    close_process_iterator(&it);
}

void test_process_group_wrong_pid() {
    struct process_group pgroup;
    CHECK(init_process_group(&pgroup, -1, 0) == 0);
    CHECK(pgroup.proclist->count == 0);
    CHECK(update_process_group(&pgroup) == 0);
    CHECK(pgroup.proclist->count == 0);
    //re-initializing a live group would leak its list, so close it first
    CHECK(close_process_group(&pgroup) == 0);
    CHECK(init_process_group(&pgroup, 9999999, 0) == 0);
    CHECK(pgroup.proclist->count == 0);
    CHECK(update_process_group(&pgroup) == 0);
    CHECK(pgroup.proclist->count == 0);
    CHECK(close_process_group(&pgroup) == 0);
}

//reading the command line costs two system calls per process, so it is opt-in
void test_command_is_opt_in() {
#ifdef __linux__
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    memset(&process, 0x7f, sizeof(process));
    filter_for(&filter, getpid(), 0, 0);
    init_process_iterator(&it, &filter);
    CHECK(get_next_process(&it, &process) == 0);
    CHECK(process.command[0] == '\0');
    CHECK(process.cpu_usage == -1);  //never left indeterminate
    close_process_iterator(&it);
#endif
}

//The old parser split /proc/<pid>/stat on spaces, so a process name containing spaces or
//parentheses shifted every later field (ppid, cputime, starttime).
void test_hostile_process_name() {
#ifdef __linux__
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    int ready[2];
    pid_t kid;
    char byte;
    CHECK(pipe(ready) == 0);
    kid = fork();
    CHECK(kid >= 0);
    if (kid == 0) {
        TEST_DIE_WITH_PARENT();
        prctl(PR_SET_NAME, "a b) (c S 1 2", 0, 0, 0);
        byte = 'x';
        if (write(ready[1], &byte, 1) != 1) {
            _exit(1);
        }
        for (;;) {
            pause();
        }
    }
    test_track_child(kid);
    CHECK(read(ready[0], &byte, 1) == 1);
    close(ready[0]);
    close(ready[1]);
    filter_for(&filter, kid, 0, 0);
    init_process_iterator(&it, &filter);
    CHECK(get_next_process(&it, &process) == 0);
    CHECK(process.pid == kid);
    CHECK(process.ppid == getpid());
    CHECK(process.starttime > 0);
    CHECK(get_next_process(&it, &process) != 0);
    close_process_iterator(&it);
    reap(kid);
#endif
}

//a zombie cannot use CPU and must not be reported (or the limiter would never see its target end)
void test_zombie_is_not_reported() {
#ifdef __linux__
    struct process_iterator it;
    struct process process;
    struct process_filter filter;
    siginfo_t info;
    pid_t kid = fork();
    int count = 0;
    CHECK(kid >= 0);
    if (kid == 0) {
        _exit(0);
    }
    //wait until it has exited, without reaping it: it is now a zombie
    CHECK(waitid(P_PID, (id_t)kid, &info, WEXITED | WNOWAIT) == 0);
    filter_for(&filter, kid, 0, 0);
    init_process_iterator(&it, &filter);
    CHECK(get_next_process(&it, &process) != 0);
    close_process_iterator(&it);
    filter_for(&filter, getpid(), 1, 0);
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &process) == 0) {
        CHECK(process.pid != kid);
        count++;
    }
    CHECK(count == 1);  //only this process
    close_process_iterator(&it);
    waitpid(kid, NULL, 0);
#endif
}

//descendants are found through the whole ancestry, not only direct children
static void chain(int depth) {
    //each level forks the next one and only the child continues the loop, so the
    //processes form a line: top -> child -> grandchild -> ...
    while (depth > 0) {
        if (fork() != 0) {
            break;  //the parent stays at its level
        }
        TEST_DIE_WITH_PARENT();
        depth--;
    }
    for (;;) {
        pause();
    }
}

void test_deep_descendants() {
    struct process_filter filter;
    pid_t top = fork();
    int attempt, count = 0;
    CHECK(top >= 0);
    if (top == 0) {
        TEST_DIE_WITH_PARENT();
        setpgid(0, 0);  //so the whole chain can be killed at once
        chain(3);       //top, child, grandchild, great-grandchild
    }
    test_track_child(top);
    for (attempt = 0; attempt < 100; attempt++) {  //the chain is created asynchronously
        struct process_iterator it;
        struct process process;
        struct timespec pause_for = { 0, 50000000 };
        filter_for(&filter, top, 1, 0);
        count = 0;
        init_process_iterator(&it, &filter);
        while (get_next_process(&it, &process) == 0) {
            count++;
        }
        close_process_iterator(&it);
        if (count == 4) {
            break;
        }
        nanosleep(&pause_for, NULL);
    }
    CHECK(count == 4);
    kill(-top, SIGKILL);
    waitpid(top, NULL, 0);
}

/* Under qemu-user emulation (a foreign-architecture container) /proc/<pid>/cmdline of another
 * process starts with the emulator, not with the program, so a process cannot be found by its
 * command. qemu fakes /proc/self and /proc/<own pid>, so ask about the CHILD from here. */
static int cmdline_starts_with(pid_t pid, const char *argv0) {
#ifdef __linux__
    char path[64], buf[PATH_MAX + 1];
    ssize_t n;
    int fd;
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 1;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return 1;
    }
    buf[n] = '\0';
    if (strcmp(buf, argv0) != 0) {
        fprintf(stderr, "note: another process's cmdline starts with '%s', not '%s' (emulator?): find-by-name tests skipped\n", buf, argv0);
        return 0;
    }
#else
    (void)pid;
    (void)argv0;
#endif
    return 1;
}

//find_process_by_name() used to call kill(-1, SIGCONT): every process of the user was resumed
void test_find_by_name(const char *argv0) {
    pid_t kid = fork();
    int status;
    int found;
    CHECK(kid >= 0);
    if (kid == 0) {
        TEST_DIE_WITH_PARENT();
        raise(SIGSTOP);
        _exit(0);
    }
    test_track_child(kid);
    CHECK(waitpid(kid, &status, WUNTRACED) == kid && WIFSTOPPED(status));
    if (!cmdline_starts_with(kid, argv0)) {
        reap(kid);
        return;
    }
    found = find_process_by_name(base_name(argv0));
    CHECK(found > 0);                 //this very program matches
    CHECK(find_process_by_name(argv0) > 0 || strchr(argv0, '/') == NULL);  //by path, when given one
    CHECK(find_process_by_name("") == 0);
    CHECK(find_process_by_name("no-such-program-xyzzy") == 0);
    //the stopped child must still be stopped
    CHECK(waitpid(kid, &status, WNOHANG | WCONTINUED) == 0);
    reap(kid);
}

void test_find_by_pid() {
    CHECK(find_process_by_pid(getpid()) == getpid());
    CHECK(find_process_by_pid(2147483646) == 0);  //no such process
    CHECK(find_process_by_pid(0) == 0);
    CHECK(find_process_by_pid(-5) == 0);
    {
        int r = find_process_by_pid(1);  //exists; controllable only as root
        CHECK(r == 1 || r == -1);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    test_single_process();
    test_multiple_process();
    test_all_processes();
    test_process_group_all();
    test_process_group_single(0);
    test_process_group_single(1);
    test_process_group_wrong_pid();
    test_process_name(argv[0]);
    test_command_is_opt_in();
    test_hostile_process_name();
    test_zombie_is_not_reported();
    test_deep_descendants();
    test_find_by_name(argv[0]);
    test_find_by_pid();
    return 0;
}
