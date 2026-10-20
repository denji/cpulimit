# cpulimit - GNU make required (use gmake on the BSDs and illumos/Solaris).
#
# Targets:    all  check  man  install  uninstall  static  static-install
#             strip  clean  distclean  fuzz-standalone  fuzz-libfuzzer  coverage
# Variables:  PREFIX DESTDIR BINDIR MANDIR CC CFLAGS CPPFLAGS LDFLAGS
#             BUILD (object/output directory)  WERROR=1  STATIC_CC

.DELETE_ON_ERROR:
.SUFFIXES:

NAME       := cpulimit
PREFIX     ?= /usr/local
DESTDIR    ?=
BINDIR     ?= $(PREFIX)/bin
MANDIR     ?= $(PREFIX)/share/man/man1
INSTALL    ?= install
STRIP      ?= strip
STATIC_CC  ?= musl-gcc
CFLAGS     ?= -O2 -g
BUILD      ?= build
WERROR     ?=

MAN_FILE   := man/$(NAME).1
UNAME      := $(shell uname -s)

SRCDIR     := src
TESTDIR    := test
BIN        := $(BUILD)/$(NAME)
COMPAT_BIN := $(SRCDIR)/$(NAME)
FUZZ_DIR   := fuzz

# Mandatory flags live here so a user-supplied CFLAGS cannot drop them.
ALL_CPPFLAGS := -D_GNU_SOURCE -I$(SRCDIR) $(CPPFLAGS)
ALL_CFLAGS   := -Wall -Wextra $(if $(WERROR),-Werror) $(CFLAGS)

LDLIBS_PLAT := -lm
ifeq ($(UNAME),FreeBSD)
LDLIBS_PLAT += -lkvm
endif

LIB_OBJS   := $(addprefix $(BUILD)/,list.o process_iterator.o process_group.o util.o limiter.o procfs_parse.o)
# modules used by the executable only (they pull in syslog, fork, fcntl and the CLI)
APP_OBJS   := $(addprefix $(BUILD)/,cli.o lock.o daemon.o log.o)
MAIN_OBJ   := $(BUILD)/cpulimit.o
UNIT_TESTS := process_iterator_test util_test limiter_test procfs_parse_test group_test list_test cli_parse_test lock_test
TEST_BINS  := $(addprefix $(BUILD)/test/,$(UNIT_TESTS) busy spawn)

.PHONY: analyze verif-sim verif-cbmc stress bench fuzz-standalone fuzz-libfuzzer coverage all check test man install uninstall static static-install static_install \
        strip clean distclean FORCE

all: $(BIN)
	@cp -f $(BIN) $(COMPAT_BIN)

# Rebuild everything when the compiler or any flag changes.
$(BUILD)/.flags: FORCE
	@mkdir -p $(BUILD)/test
	@printf '%s\n' '$(CC) | $(ALL_CPPFLAGS) | $(ALL_CFLAGS) | $(LDFLAGS) | $(LDLIBS_PLAT)' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi

$(BUILD)/%.o: $(SRCDIR)/%.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -MMD -MP $(ALL_CFLAGS) -c -o $@ $<

$(BIN): $(MAIN_OBJ) $(APP_OBJS) $(LIB_OBJS)
	$(CC) -o $@ $(MAIN_OBJ) $(APP_OBJS) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS_PLAT)

$(BUILD)/test/%_test: $(TESTDIR)/%_test.c $(APP_OBJS) $(LIB_OBJS) $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -I$(TESTDIR) -UNDEBUG -MMD -MP $(ALL_CFLAGS) -o $@ $< $(APP_OBJS) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS_PLAT)

# The group tests run process_group.c against a scripted iterator and an allocator that can
# fail on demand, so list.c and process_group.c are rebuilt here with the allocation seam on.
GT_DEFS := -DCL_ALLOC_HEADER='"fault_alloc.h"' -I$(TESTDIR)

$(BUILD)/test/gt_%.o: $(SRCDIR)/%.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) $(GT_DEFS) -MMD -MP $(ALL_CFLAGS) -c -o $@ $<

$(BUILD)/test/group_test: $(TESTDIR)/group_test.c $(TESTDIR)/fake_iterator.c $(TESTDIR)/fault_alloc.c \
        $(BUILD)/test/gt_list.o $(BUILD)/test/gt_process_group.o $(BUILD)/util.o $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -I$(TESTDIR) -UNDEBUG $(ALL_CFLAGS) -o $@ $(filter %.c %.o,$^) $(LDFLAGS)

$(BUILD)/test/list_test: $(TESTDIR)/list_test.c $(TESTDIR)/fault_alloc.c $(BUILD)/test/gt_list.o $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -I$(TESTDIR) -UNDEBUG $(ALL_CFLAGS) -o $@ $(filter %.c %.o,$^) $(LDFLAGS)

$(BUILD)/test/busy: $(TESTDIR)/busy.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -o $@ $< $(LDFLAGS) -lpthread

$(BUILD)/test/spawn: $(TESTDIR)/spawn.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -o $@ $< $(LDFLAGS)

# The man page is hand-written (man/cpulimit.1); `make man` only checks that it formats.
man:
	@if command -v mandoc >/dev/null 2>&1; then mandoc -Tlint -Wwarning $(MAN_FILE); \
	 elif command -v groff >/dev/null 2>&1; then groff -man -ww -z $(MAN_FILE); \
	 else echo "man: no mandoc or groff, not checked" >&2; fi

# "test" is also a directory name, so it must stay phony.
check test: all $(TEST_BINS)
	BUILD=$(BUILD) sh $(TESTDIR)/run_tests.sh

strip: all
	$(STRIP) $(BIN)
	@cp -f $(BIN) $(COMPAT_BIN)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(NAME)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -m 644 $(MAN_FILE) $(DESTDIR)$(MANDIR)/$(NAME).1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME) $(DESTDIR)$(MANDIR)/$(NAME).1

# Static build in its own object directory, so it never mixes with a dynamic one.
STATIC_VARS = BUILD=$(BUILD)/static CC=$(STATIC_CC) LDFLAGS="$(LDFLAGS) -static"

static:
	@command -v $(STATIC_CC) >/dev/null 2>&1 || \
	    { echo "static: $(STATIC_CC) not found (set STATIC_CC=...)" >&2; exit 1; }
	$(MAKE) $(STATIC_VARS) all

static-install static_install: static
	$(MAKE) $(STATIC_VARS) strip
	$(MAKE) $(STATIC_VARS) install

clean:
	@test -n "$(BUILD)" && test "$(BUILD)" != "/"
	rm -rf $(BUILD)

distclean:
	rm -rf build $(COMPAT_BIN)

FORCE:

# ---- fuzzing -------------------------------------------------------------------------------
# fuzz-standalone: deterministic mutation driver, only needs a C compiler with ASan/UBSan.
#   make fuzz-standalone FUZZ_ITERATIONS=1000000 FUZZ_SEED=7
# fuzz-libfuzzer: coverage-guided (needs clang with the libFuzzer runtime); builds the
#   targets, run them with e.g.  build/fuzz/fuzz_cli fuzz/corpus/cli -max_total_time=60
FUZZ_ITERATIONS ?= 200000
FUZZ_SEED       ?= 1
FUZZ_SAN        ?= -fsanitize=address,undefined -fno-sanitize-recover=undefined
FUZZ_SRC_procfs := $(SRCDIR)/procfs_parse.c
FUZZ_SRC_cli    := $(SRCDIR)/cli.c $(SRCDIR)/util.c

fuzz-standalone:
	@mkdir -p $(BUILD)/fuzz
	$(CC) $(ALL_CPPFLAGS) -O1 -g $(FUZZ_SAN) -o $(BUILD)/fuzz/standalone_procfs \
	    $(FUZZ_DIR)/fuzz_procfs_parse.c $(FUZZ_DIR)/standalone_driver.c $(FUZZ_SRC_procfs)
	$(CC) $(ALL_CPPFLAGS) -O1 -g $(FUZZ_SAN) -o $(BUILD)/fuzz/standalone_cli \
	    $(FUZZ_DIR)/fuzz_cli.c $(FUZZ_DIR)/standalone_driver.c $(FUZZ_SRC_cli) -lm
	cd $(BUILD)/fuzz && ./standalone_procfs -n $(FUZZ_ITERATIONS) -s $(FUZZ_SEED) $(CURDIR)/$(FUZZ_DIR)/corpus/procfs
	cd $(BUILD)/fuzz && ./standalone_cli -n $(FUZZ_ITERATIONS) -s $(FUZZ_SEED) $(CURDIR)/$(FUZZ_DIR)/corpus/cli

fuzz-libfuzzer:
	@mkdir -p $(BUILD)/fuzz
	clang $(ALL_CPPFLAGS) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
	    -o $(BUILD)/fuzz/fuzz_procfs_parse $(FUZZ_DIR)/fuzz_procfs_parse.c $(FUZZ_SRC_procfs)
	clang $(ALL_CPPFLAGS) -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
	    -o $(BUILD)/fuzz/fuzz_cli $(FUZZ_DIR)/fuzz_cli.c $(FUZZ_SRC_cli) -lm

# ---- load, memory and benchmark ---------------------------------------------------------------
stress: all $(BUILD)/test/busy
	BUILD=$(BUILD) sh $(TESTDIR)/stress_test.sh

# BASELINE=/path/to/other/cpulimit  make bench
bench: all $(BUILD)/test/busy
	@test -n "$(BASELINE)" || { echo "bench: set BASELINE=<path to the binary to compare against>" >&2; exit 1; }
	sh bench/bench.sh $(BASELINE) $(BIN) $(BUILD)/test/busy $(BENCH_SECONDS)

BENCH_SECONDS ?= 8

# ---- static analysis and verification -------------------------------------------------------
# Platform iterators other than the one this host builds are not analysable here.
ANALYZE_SRCS := $(filter-out $(SRCDIR)/process_iterator_%.c,$(wildcard $(SRCDIR)/*.c))

# gcc -fanalyzer is a hard gate; clang-tidy runs when installed (config in .clang-tidy).
analyze:
	@set -e; for f in $(ANALYZE_SRCS); do \
	    out=$$($(CC) -fanalyzer -O1 -Wall -Wextra $(ALL_CPPFLAGS) -c $$f -o /dev/null 2>&1) || { echo "$$out"; exit 1; }; \
	    if [ -n "$$out" ]; then echo "$$out"; exit 1; fi; done; echo "gcc -fanalyzer: clean"
	@if command -v clang-tidy >/dev/null 2>&1; then clang-tidy -quiet $(ANALYZE_SRCS) -- $(ALL_CPPFLAGS) && echo "clang-tidy: clean"; \
	 else echo "clang-tidy not installed, skipped"; fi

# Properties of the pure functions: random testing here, proof for a bound under CBMC.
verif-sim:
	CC=$(CC) BUILD=$(BUILD) sh scripts/verif.sh sim

verif-cbmc:
	BUILD=$(BUILD) sh scripts/verif.sh cbmc

# ---- coverage ------------------------------------------------------------------------------
# Builds with gcov instrumentation in its own directory, runs the whole test suite and prints
# per-file line and branch coverage (scripts/coverage.sh).
coverage:
	CC=$(CC) BUILD=$(BUILD)/cov sh scripts/coverage.sh

-include $(wildcard $(BUILD)/*.d $(BUILD)/test/*.d)
