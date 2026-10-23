#include <string.h>

#include "platform/iterator.h"
#include "support/fake_iterator.h"

static const struct fake_proc *snapshot = NULL;
static int count = 0;
static int pos = 0;
static int opened = 0;
static int closed = 0;

void fake_set_snapshot(const struct fake_proc *procs, int n) {
    snapshot = procs;
    count = n;
}

int fake_open_iterators(void) {
    return opened - closed;
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter) {
    memset(it, 0, sizeof(*it));
    it->filter = filter;
    pos = 0;
    opened++;
    return 0;
}

int get_next_process(struct process_iterator *it, struct process *p) {
    (void)it;
    if (pos >= count) {
        return -1;
    }
    memset(p, 0, sizeof(*p));
    p->pid = snapshot[pos].pid;
    p->ppid = snapshot[pos].ppid;
    p->starttime = snapshot[pos].starttime;
    p->cputime = snapshot[pos].cputime;
    p->cpu_usage = -1;
    pos++;
    return 0;
}

int close_process_iterator(struct process_iterator *it) {
    (void)it;
    closed++;
    return 0;
}
