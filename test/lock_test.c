/* target_lock_acquire(): one limiter per target, safe directory handling. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "lock.h"

static char base[256];

static void make_base(void) {
    const char *t = getenv("TMPDIR");
    snprintf(base, sizeof(base), "%s/cpulimit-locktest-%ld", t != NULL ? t : "/tmp", (long)getpid());
    CHECK(mkdir(base, 0700) == 0);
}

static void rmtree(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", base);
    if (system(cmd) != 0) {
        fprintf(stderr, "note: could not remove %s\n", base);
    }
}

static void path_in_base(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", base, name);
}

/* a child that takes the lock, reports, and holds it until told to leave */
struct holder {
    pid_t pid;
    int release_fd;  /* closing this lets the child exit */
};

static struct holder start_holder(pid_t target, int64_t start, const char *dir) {
    int ready[2], release[2];
    struct holder h;
    char byte;
    CHECK(pipe(ready) == 0 && pipe(release) == 0);
    h.pid = fork();
    CHECK(h.pid >= 0);
    if (h.pid == 0) {
        int fd;
        close(ready[0]);
        close(release[1]);
        if (target_lock_acquire(target, start, dir, &fd, NULL) != LOCK_ACQUIRED) {
            _exit(2);
        }
        byte = 'k';
        if (write(ready[1], &byte, 1) != 1) {
            _exit(3);
        }
        /* blocks until the parent closes its end; the result does not matter, only that it returns */
        if (read(release[0], &byte, 1) < 0) {
            _exit(0);
        }
        _exit(0);
    }
    close(ready[1]);
    close(release[0]);
    CHECK(read(ready[0], &byte, 1) == 1 && byte == 'k');
    close(ready[0]);
    h.release_fd = release[1];
    return h;
}

static void stop_holder(struct holder *h) {
    int status;
    close(h->release_fd);
    CHECK(waitpid(h->pid, &status, 0) == h->pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_exclusive(void) {
    struct holder h;
    int fd = -1;
    pid_t who = 0;
    h = start_holder(1234, 777, base);
    CHECK(target_lock_acquire(1234, 777, base, &fd, &who) == LOCK_BUSY);
    CHECK(who == h.pid);                                              /* it tells who holds it */
    CHECK(fd == -1);
    /* a different start time is a different process that merely reuses the pid */
    CHECK(target_lock_acquire(1234, 778, base, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
    CHECK(target_lock_acquire(1235, 777, base, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
    stop_holder(&h);
    /* the holder is gone: the kernel released the lock, nothing stale is left */
    CHECK(target_lock_acquire(1234, 777, base, &fd, &who) == LOCK_ACQUIRED);
    target_lock_release(fd);
}

static void test_killed_holder_releases(void) {
    struct holder h;
    int fd = -1, status;
    h = start_holder(4321, 5, base);
    CHECK(target_lock_acquire(4321, 5, base, &fd, NULL) == LOCK_BUSY);
    kill(h.pid, SIGKILL);                       /* no cleanup code runs */
    CHECK(waitpid(h.pid, &status, 0) == h.pid);
    close(h.release_fd);
    CHECK(target_lock_acquire(4321, 5, base, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
}

static void test_release_and_reacquire(void) {
    int fd1 = -1, fd2 = -1;
    CHECK(target_lock_acquire(50, 1, base, &fd1, NULL) == LOCK_ACQUIRED);
    CHECK(fd1 >= 0);
    target_lock_release(fd1);
    CHECK(target_lock_acquire(50, 1, base, &fd2, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd2);
    target_lock_release(-1);                    /* harmless */
}

static void test_directory_hardening(void) {
    char dir[512], target[512], file[1100];
    int fd = -1;
    struct stat st;
    /* created private */
    path_in_base(dir, sizeof(dir), "fresh");
    CHECK(target_lock_acquire(9, 9, dir, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
    CHECK(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700);

    /* an existing directory that others can write to is not trusted */
    path_in_base(dir, sizeof(dir), "open");
    CHECK(mkdir(dir, 0777) == 0);
    CHECK(chmod(dir, 0777) == 0);
    errno = 0;
    CHECK(target_lock_acquire(9, 9, dir, &fd, NULL) == LOCK_ERROR && errno == EACCES);

    /* a symbolic link in place of the directory is not followed */
    path_in_base(target, sizeof(target), "real");
    CHECK(mkdir(target, 0700) == 0);
    path_in_base(dir, sizeof(dir), "link");
    CHECK(symlink(target, dir) == 0);
    errno = 0;
    CHECK(target_lock_acquire(9, 9, dir, &fd, NULL) == LOCK_ERROR);
    CHECK(errno == EACCES);

    /* a symbolic link in place of the lock file is not written through */
    path_in_base(dir, sizeof(dir), "trap");
    CHECK(mkdir(dir, 0700) == 0);
    path_in_base(target, sizeof(target), "victim");
    {
        FILE *f = fopen(target, "w");
        CHECK(f != NULL);
        fputs("precious", f);
        fclose(f);
    }
    snprintf(file, sizeof(file), "%s/%ld-%lld.lock", dir, 7L, 8LL);
    CHECK(symlink(target, file) == 0);
    CHECK(target_lock_acquire(7, 8, dir, &fd, NULL) == LOCK_ERROR);
    {
        char buf[16] = { 0 };
        FILE *f = fopen(target, "r");
        CHECK(f != NULL && fgets(buf, sizeof(buf), f) != NULL);
        fclose(f);
        CHECK(strcmp(buf, "precious") == 0);                              /* untouched */
    }

    /* a path that cannot fit is an error, not a truncation */
    {
        char longdir[1024];
        memset(longdir, 'a', sizeof(longdir) - 1);
        longdir[0] = '/';
        longdir[sizeof(longdir) - 1] = '\0';
        CHECK(target_lock_acquire(1, 1, longdir, &fd, NULL) == LOCK_ERROR);
    }
}

static void test_default_directory(void) {
    int fd = -1;
    char xdg[512];
    const char *old = getenv("XDG_RUNTIME_DIR");
    /* under XDG_RUNTIME_DIR when it is a directory we own, else /tmp/cpulimit-<uid> */
    path_in_base(xdg, sizeof(xdg), "xdg");
    CHECK(mkdir(xdg, 0700) == 0);
    CHECK(setenv("XDG_RUNTIME_DIR", xdg, 1) == 0);
    CHECK(target_lock_acquire(31, 2, NULL, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
    {
        char expect[600];
        snprintf(expect, sizeof(expect), "%s/cpulimit/31-2.lock", xdg);
        CHECK(access(expect, F_OK) == 0);
    }
    CHECK(setenv("XDG_RUNTIME_DIR", "relative/path", 1) == 0);           /* ignored: must be absolute */
    CHECK(target_lock_acquire(32, 2, NULL, &fd, NULL) == LOCK_ACQUIRED);
    target_lock_release(fd);
    {
        char expect[128];
        snprintf(expect, sizeof(expect), "/tmp/cpulimit-%lu/32-2.lock", (unsigned long)geteuid());
        CHECK(access(expect, F_OK) == 0);
        (void)unlink(expect);
    }
    if (old != NULL) {
        setenv("XDG_RUNTIME_DIR", old, 1);
    } else {
        unsetenv("XDG_RUNTIME_DIR");
    }
}

int main(void) {
    make_base();
    test_exclusive();
    test_killed_holder_releases();
    test_release_and_reacquire();
    test_directory_hardening();
    test_default_directory();
    rmtree();
    return 0;
}
