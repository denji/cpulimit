/* spawn [-i SIG]... [--] command [args...]
 *
 * Start a command with a KNOWN signal state, so tests do not depend on how the shell started
 * them: a shell runs background jobs with SIGINT and SIGQUIT ignored, which would change what
 * cpulimit (rightly) does with them. All of SIGHUP, SIGINT, SIGQUIT, SIGTERM and SIGPIPE are
 * reset to their defaults and unblocked, except the ones named with -i, which are ignored (as
 * "nohup" leaves SIGHUP). The command replaces this process, so $! stays its pid. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int signal_number(const char *name) {
    static const struct { const char *name; int sig; } table[] = {
        { "HUP", SIGHUP }, { "INT", SIGINT }, { "QUIT", SIGQUIT }, { "TERM", SIGTERM }, { "PIPE", SIGPIPE }
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) {
            return table[i].sig;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    static const int all[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGPIPE };
    int ignored[8] = { 0 };
    int nignored = 0, i, k;
    sigset_t none;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            int sig = signal_number(argv[++i]);
            if (sig < 0 || nignored == 8) {
                fprintf(stderr, "spawn: bad signal '%s'\n", argv[i]);
                return 2;
            }
            ignored[nignored++] = sig;
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            break;
        }
    }
    if (i >= argc) {
        fprintf(stderr, "usage: spawn [-i SIG]... [--] command [args...]\n");
        return 2;
    }
    for (k = 0; k < (int)(sizeof(all) / sizeof(all[0])); k++) {
        signal(all[k], SIG_DFL);
    }
    for (k = 0; k < nignored; k++) {
        signal(ignored[k], SIG_IGN);
    }
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    execvp(argv[i], &argv[i]);
    perror("spawn");
    return 127;
}
