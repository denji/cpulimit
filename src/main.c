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
 *
 **************************************************************
 *
 * This is a simple program to limit the cpu usage of a process
 * If you modify this code, send me a copy please
 *
 * Get the latest version at: http://github.com/denji/cpulimit
 *
 * Design and invariants: docs/ARCHITECTURE.md
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cli/cli.h"
#include "runtime/daemon.h"
#include "core/limiter.h"
#include "core/list.h"
#include "runtime/lock.h"
#include "runtime/log.h"
#include "core/process_group.h"
#include "util/util.h"

#define MAX_PRIORITY (-10)

/* GLOBAL VARIABLES */

//the "family"
static struct process_group pgroup;
//pid of cpulimit
static pid_t cpulimit_pid;
//name of this program (maybe cpulimit...)
static const char *program_name;

//verbose mode
static int verbose = 0;
//do not try to raise our own priority
static int no_boost = 0;

//the signal that asked us to stop, or 0. Written only by the handler (async-signal-safe)
static volatile sig_atomic_t stop_signal = 0;
//signal mask to wait with: the original one, i.e. with the stop signals deliverable
static sigset_t wait_mask;

static void on_stop_signal(int sig) {
    stop_signal = sig;
}

/* the signals that mean "stop limiting and let the target go" */
static const int stop_signals[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
#define NSTOP ((int)(sizeof(stop_signals) / sizeof(stop_signals[0])))

/*
 * Catch the stop signals, but keep them BLOCKED except while waiting (pselect with
 * wait_mask), so one can never arrive between a check of stop_signal and the sleep.
 * A signal that was ignored when we started (nohup, a background job) stays ignored.
 */
static void install_signals(void) {
    sigset_t block;
    struct sigaction sa, old;
    int i;
    sigemptyset(&block);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    /* no SA_RESTART: interrupted waits must return so the flag is looked at */
    for (i = 0; i < NSTOP; i++) {
        if (sigaction(stop_signals[i], NULL, &old) == 0 && old.sa_handler == SIG_IGN) {
            continue;
        }
        if (sigaction(stop_signals[i], &sa, NULL) == 0) {
            sigaddset(&block, stop_signals[i]);
        }
    }
    /* a closed pipe on stdout must not kill us while a target is stopped */
    signal(SIGPIPE, SIG_IGN);
    sigprocmask(SIG_BLOCK, &block, &wait_mask);
}

//let all the processes we know of continue, in case we stopped them
static void release_all(struct process_group *pg) {
    int i;
    for (i = 0; i < PIDHASH_SZ; i++) {
        struct list_node *node;
        if (pg->proctable[i] == NULL) {
            continue;
        }
        for (node = pg->proctable[i]->first; node != NULL; node = node->next) {
            kill(((struct process *)node->data)->pid, SIGCONT);
        }
    }
}

//return t1-t2 in microseconds (no overflow checks, so better watch out!)
static inline unsigned long timediff(const struct timeval *t1, const struct timeval *t2) {
    return (t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_usec - t2->tv_usec);
}

static void usage_and_exit(FILE *stream, int ncpu, int exit_code) {
    cli_usage(stream, program_name, ncpu);
    exit(exit_code);
}

static void increase_priority(void) {
    //find the best available nice value
    int old_priority = getpriority(PRIO_PROCESS, 0);
    int priority = old_priority;
    while (priority > MAX_PRIORITY && setpriority(PRIO_PROCESS, 0, priority - 1) == 0) {
        priority--;
    }
    if (priority != old_priority) {
        if (verbose) {
            printf("Priority changed to %d\n", priority);
        }
    } else {
        if (verbose) {
            printf("Warning: Cannot change priority. Run as root or renice for best results.\n");
        }
    }
}

/* Get the number of CPUs; never less than 1 */
static int get_ncpu(void) {
    long ncpu = -1;
#ifdef _SC_NPROCESSORS_ONLN
    ncpu = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return ncpu < 1 ? 1 : (ncpu > 65536 ? 65536 : (int)ncpu);
}

/* the kernel's pid limit, or -1 when it cannot be read (then only the lower bound is checked) */
static long get_pid_max(void) {
#ifdef __linux__
    //read /proc/sys/kernel/pid_max
    char buffer[64];
    long value = -1;
    FILE *fd = fopen("/proc/sys/kernel/pid_max", "r");
    if (fd == NULL) {
        return -1;
    }
    if (fgets(buffer, sizeof(buffer), fd) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
        if (parse_long(buffer, 1, 1L << 30, &value) != 0) {
            value = -1;
        }
    }
    fclose(fd);
    return value;
#else
    return 99998;
#endif
}

/* start time of a process (the second half of its identity), or -1 if it is gone */
static int64_t starttime_of(pid_t pid) {
    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    int64_t result = -1;
    memset(&filter, 0, sizeof(filter));
    filter.pid = pid;
    if (init_process_iterator(&it, &filter) != 0) {
        return -1;
    }
    if (get_next_process(&it, &proc) == 0) {
        result = proc.starttime;
    }
    close_process_iterator(&it);
    return result;
}

enum limit_end { LIMIT_TARGET_GONE, LIMIT_SIGNALLED, LIMIT_FAILED };

static enum limit_end limit_process(pid_t pid, double limit, int include_children, double minimum_cpu_usage,
                                    int chunk_size) {
    //slices of the slot: in which the process is allowed to run / is stopped
    struct slices slice;
    //when the last work slice has started
    struct timeval startwork;
    //when the last work slice has finished
    struct timeval endwork;
    //initialization
    memset(&slice, 0, sizeof(struct slices));
    memset(&startwork, 0, sizeof(struct timeval));
    memset(&endwork, 0, sizeof(struct timeval));
    //last working time in microseconds
    unsigned long workingtime = 0;
    //generic list item
    struct list_node *node;
    //counter
    int c = 0;
    enum limit_end end = LIMIT_TARGET_GONE;

    //get a better priority
    if (!no_boost) {
        increase_priority();
    }

    //build the family
    if (init_process_group(&pgroup, pid, include_children) != 0) {
        report(LOG_ERR, stderr, "Error: out of memory\n");
        return LIMIT_FAILED;
    }

    if (verbose) {
        report(LOG_INFO, stdout, "Members in the process group owned by %d: %d\n", pgroup.target_pid,
               pgroup.proclist->count);
    }

    //rate at which we are keeping active the processes (range 0-1)
    //1 means that the process are using all the twork slice
    double workingrate = -1;
    while (1) {
        if (stop_signal) {
            end = LIMIT_SIGNALLED;
            break;
        }
        if (update_process_group(&pgroup) != 0) {
            report(LOG_ERR, stderr, "Error: out of memory\n");
            end = LIMIT_FAILED;
            break;
        }

        if (pgroup.proclist->count == 0) {
            if (verbose) {
                report(LOG_INFO, stdout, "No more processes.\n");
            }
            break;
        }

        //total cpu actual usage (range 0-1)
        //1 means that the processes are using 100% cpu
        double pcpu = -1;

        //estimate how much the controlled processes are using the cpu in the working interval
        for (node = pgroup.proclist->first; node != NULL; node = node->next) {
            struct process *proc = (struct process *)(node->data);
            if (proc->cpu_usage < 0) {
                continue;
            }
            if (pcpu < 0) {
                pcpu = 0;
            }
            pcpu += proc->cpu_usage;
        }

        //adjust work and sleep time slices (pcpu < 0: it's the 1st cycle)
        compute_slices(limit, pcpu, workingrate, chunk_size, &slice);
        workingrate = slice.rate;
        if (pcpu < 0) {
            pcpu = limit;
        }

        if (verbose) {
            if (c % 200 == 0) {
                printf("\n%%CPU\twork quantum\tsleep quantum\tactive rate\n");
            }
            if (c % 10 == 0 && c > 0) {
                printf("%0.2lf%%\t%6ld us\t%6ld us\t%0.2lf%%\n", pcpu * 100, (long)(slice.work_ns / 1000),
                       (long)(slice.stop_ns / 1000), workingrate * 100);
            }
        }

        //resume processes
        node = pgroup.proclist->first;
        while (node != NULL) {
            struct list_node *next_node = node->next;
            struct process *proc = (struct process *)(node->data);
            if (kill(proc->pid, SIGCONT) != 0) {
                //process is dead, remove it from family
                if (verbose) {
                    fprintf(stderr, "SIGCONT failed. Process %d dead!\n", proc->pid);
                }
                //remove process from group (and from the list)
                remove_process(&pgroup, proc->pid);
            }
            node = next_node;
        }

        //now processes are free to run (same working slice for all)
        gettimeofday(&startwork, NULL);
        wait_ns(slice.work_ns, &wait_mask);
        gettimeofday(&endwork, NULL);
        workingtime = timediff(&endwork, &startwork);

        long delay = workingtime - slice.work_ns / 1000;
        if (c > 0 && delay > 10000) {
            //delay is too much! signal to user?
            //fprintf(stderr, "%d %ld us\n", c, delay);
        }
        if (stop_signal) {
            end = LIMIT_SIGNALLED;
            break;
        }

        if (slice.stop_ns > 0) {
            //stop processes only if the stop slice is not empty
            node = pgroup.proclist->first;
            while (node != NULL) {
                struct list_node *next_node = node->next;
                struct process *proc = (struct process *)(node->data);
                if (proc->cpu_usage > minimum_cpu_usage && kill(proc->pid, SIGSTOP) != 0) {
                    //process is dead, remove it from family
                    if (verbose) {
                        fprintf(stderr, "SIGSTOP failed. Process %d dead!\n", proc->pid);
                    }
                    //remove process from group (and from the list)
                    remove_process(&pgroup, proc->pid);
                }
                node = next_node;
            }
            //now the processes are sleeping
            wait_ns(slice.stop_ns, &wait_mask);
        }
        c++;
    }
    //invariant S1: whatever the reason we leave, nothing is left stopped by us
    release_all(&pgroup);
    close_process_group(&pgroup);
    return end;
}

//wait for a child to end, without ever missing a stop signal; returns 0 and its status, or -1 if signalled
static int reap_child(pid_t child, int *status) {
    for (;;) {
        pid_t r = waitpid(child, status, WNOHANG);
        if (r == child) {
            return 0;
        }
        if (r < 0 && errno != EINTR) {
            *status = 0;
            return 0;  //not our child any more: nothing to wait for
        }
        if (stop_signal) {
            return -1;
        }
        wait_ns(100000000LL, &wait_mask);
    }
}

//run the command as a child and limit it; never returns
static void run_command(const struct cli_config *cfg, char **argv, int argc, double limit) {
    int i;
    int status = 0;
    //executable file
    const char *cmd = argv[cfg->command_start];
    //command line arguments
    char **cmd_args = (char **)malloc((argc - cfg->command_start + 1) * sizeof(char *));
    if (cmd_args == NULL) {
        exit(2);
    }
    for (i = 0; i < argc - cfg->command_start; i++) {
        cmd_args[i] = argv[i + cfg->command_start];
    }
    cmd_args[i] = NULL;

    if (verbose) {
        printf("Running command: '%s", cmd);
        for (i = 1; i < argc - cfg->command_start; i++) {
            printf(" %s", cmd_args[i]);
        }
        printf("'\n");
    }

    //flush before forking, or the buffered text is written once per process when stdout is a pipe or file
    fflush(stdout);
    fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        exit(EXIT_FAILURE);
    } else if (child == 0) {
        //target process code: it must not inherit our signal setup
        sigprocmask(SIG_SETMASK, &wait_mask, NULL);
        signal(SIGPIPE, SIG_DFL);
        int ret = execvp(cmd, cmd_args);
        //if we are here there was an error, show it
        perror("Error");
        exit(ret);
    }
    free((void *)cmd_args);
    //parent code: it is the limiter, and the target is its child, so it learns how the target
    //ended from waitpid() and there is no second process that could be left behind
    if (verbose) {
        report(LOG_INFO, stdout, "Limiting process %d\n", child);
    }
    enum limit_end end = limit_process(child, limit, cfg->include_children, cfg->min_cpu, cfg->chunk_us);
    if (end == LIMIT_SIGNALLED) {
        //fix ^C little problem
        printf("\r");
        fflush(stdout);
        exit(0);
    }
    if (end == LIMIT_FAILED) {
        kill(child, SIGCONT);
        exit(2);
    }
    if (reap_child(child, &status) != 0) {
        kill(child, SIGCONT);
        printf("\r");
        fflush(stdout);
        exit(0);
    }
    if (WIFEXITED(status)) {
        if (verbose) {
            report(LOG_INFO, stdout, "Process %d terminated with exit status %d\n", child, (int)WEXITSTATUS(status));
        }
        exit(WEXITSTATUS(status));
    }
    printf("Process %d terminated abnormally\n", child);
    exit(status);
}

int main(int argc, char **argv) {
    struct cli_config cfg;
    pid_t pid = 0;
    int lock_fd = -1;
    int ncpu;
    long pid_max;
    enum cli_result parsed;

    //line-buffered output: when stdout is a pipe or a file it would otherwise be flushed every 4 KB,
    //so a reader (tee, a log file) would see events minutes late and a closed pipe would go unnoticed
    setvbuf(stdout, NULL, _IOLBF, 0);

    //get program name
    program_name = base_name(argv[0]);
    //get current pid
    cpulimit_pid = getpid();
    //get cpu count
    ncpu = get_ncpu();
    pid_max = get_pid_max();

    //parse arguments
    parsed = cli_parse(argc, argv, ncpu, pid_max, &cfg, stderr);
    switch (parsed) {
    case CLI_USAGE:
        usage_and_exit(stderr, ncpu, 1);
        break;
    case CLI_HELP:
        usage_and_exit(stdout, ncpu, 1);
        break;
    case CLI_VERSION:
        printf("%s\n", cli_version());
        exit(0);
    case CLI_OK:
        break;
    }
    verbose = cfg.verbose;
    no_boost = cfg.no_boost;
    pid = cfg.pid_set ? (pid_t)cfg.pid : 0;
    double limit = cfg.limit_pct / 100.0;

    if (cfg.daemonize) {
        if (daemonize(cfg.pidfile) != 0) {
            fprintf(stderr, "Error: cannot run in the background: %s\n", strerror(errno));
            exit(1);
        }
        cpulimit_pid = getpid();
        //nobody reads the console of a daemon: its events go to syslog
        log_set_console(0);
        cfg.use_syslog = 1;
    }
    if (cfg.use_syslog) {
        log_set_syslog(1, program_name);
    }

    //all arguments are ok!
    install_signals();

    //print the number of available cpu
    if (verbose) {
        printf("%d cpu detected\n", ncpu);
    }

    if (cfg.command_start != 0) {
        run_command(&cfg, argv, argc, limit);
    }

    while (1) {
        //look for the target process..or wait for it
        pid_t ret = 0;
        if (cfg.pid_set) {
            //search by pid
            ret = find_process_by_pid(pid);
            if (ret == 0) {
                report(LOG_NOTICE, stdout, "No process found\n");
            } else if (ret < 0) {
                report(LOG_NOTICE, stdout, "Process found but you aren't allowed to control it\n");
            }
        } else {
            //search by file or path name
            ret = find_process_by_name(cfg.exe);
            if (ret == 0) {
                report(LOG_NOTICE, stdout, "No process found\n");
            } else if (ret < 0) {
                report(LOG_NOTICE, stdout, "Process found but you aren't allowed to control it\n");
            } else {
                pid = ret;
            }
        }
        if (ret > 0) {
            if (ret == cpulimit_pid) {
                report(LOG_ERR, stdout, "Target process %d is cpulimit itself! Aborting because it makes no sense\n",
                       ret);
                exit(1);
            }
            //one limiter per target: two would undo each other's SIGSTOP/SIGCONT
            if (!cfg.no_lock) {
                int64_t started = starttime_of(pid);
                pid_t holder = 0;
                if (started >= 0) {
                    switch (target_lock_acquire(pid, started, NULL, &lock_fd, &holder)) {
                    case LOCK_ACQUIRED:
                        break;
                    case LOCK_BUSY:
                        report(LOG_ERR, stderr,
                               "Error: process %d is already being limited by another cpulimit (pid %d); use --no-lock "
                               "to override\n",
                               pid, (int)holder);
                        exit(1);
                    case LOCK_ERROR:
                        report(LOG_WARNING, stderr, "Warning: cannot create the lock file (%s); going on without it\n",
                               strerror(errno));
                        lock_fd = -1;
                        break;
                    }
                }
            }
            report(LOG_INFO, stdout, "Process %d found\n", pid);
            //control
            {
                enum limit_end end = limit_process(pid, limit, cfg.include_children, cfg.min_cpu, cfg.chunk_us);
                target_lock_release(lock_fd);
                lock_fd = -1;
                if (end == LIMIT_SIGNALLED) {
                    printf("\r");
                    fflush(stdout);
                    exit(0);
                }
                if (end == LIMIT_FAILED) {
                    exit(2);
                }
            }
        }
        if (cfg.lazy) {
            break;
        }
        if (stop_signal) {
            break;
        }
        wait_ns(2000000000LL, &wait_mask);
        if (stop_signal) {
            printf("\r");
            fflush(stdout);
            break;
        }
    };

    exit(0);
}
