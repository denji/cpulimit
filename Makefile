# cpulimit - GNU make required (use gmake on the BSDs and illumos/Solaris).
#
# Targets:    all  check  man  install  uninstall  static  static-install
#             strip  clean  distclean
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
HELP2MAN   ?= help2man
STATIC_CC  ?= musl-gcc
CFLAGS     ?= -O2 -g
BUILD      ?= build
WERROR     ?=

MAN_NAME   := CPU Utilization Limiter
MAN_SOURCE := https://github.com/denji/cpulimit
UNAME      := $(shell uname -s)

SRCDIR     := src
TESTDIR    := test
BIN        := $(BUILD)/$(NAME)
COMPAT_BIN := $(SRCDIR)/$(NAME)

# Mandatory flags live here so a user-supplied CFLAGS cannot drop them.
ALL_CPPFLAGS := -D_GNU_SOURCE -I$(SRCDIR) $(CPPFLAGS)
ALL_CFLAGS   := -Wall -Wextra $(if $(WERROR),-Werror) $(CFLAGS)

LDLIBS_PLAT := -lm
ifeq ($(UNAME),FreeBSD)
LDLIBS_PLAT += -lkvm
endif

LIB_OBJS   := $(addprefix $(BUILD)/,list.o process_iterator.o process_group.o util.o limiter.o procfs_parse.o)
MAIN_OBJ   := $(BUILD)/cpulimit.o
UNIT_TESTS := process_iterator_test util_test limiter_test procfs_parse_test group_test
TEST_BINS  := $(addprefix $(BUILD)/test/,$(UNIT_TESTS) busy)

HAVE_HELP2MAN := $(shell command -v $(HELP2MAN) 2>/dev/null)
ifneq ($(HAVE_HELP2MAN),)
MAN_FILE := $(BUILD)/$(NAME).1
else
MAN_FILE :=
endif

.PHONY: all check test man install uninstall static static-install static_install \
        strip clean distclean FORCE

all: $(BIN) $(MAN_FILE)
	@cp -f $(BIN) $(COMPAT_BIN)

# Rebuild everything when the compiler or any flag changes.
$(BUILD)/.flags: FORCE
	@mkdir -p $(BUILD)/test
	@printf '%s\n' '$(CC) | $(ALL_CPPFLAGS) | $(ALL_CFLAGS) | $(LDFLAGS) | $(LDLIBS_PLAT)' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi

$(BUILD)/%.o: $(SRCDIR)/%.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -MMD -MP $(ALL_CFLAGS) -c -o $@ $<

$(BIN): $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) -o $@ $(MAIN_OBJ) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS_PLAT)

$(BUILD)/test/%_test: $(TESTDIR)/%_test.c $(LIB_OBJS) $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -I$(TESTDIR) -UNDEBUG -MMD -MP $(ALL_CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS_PLAT)

# The group tests run process_group.c against a scripted iterator and an allocator that can
# fail on demand, so list.c and process_group.c are rebuilt here with the allocation seam on.
GT_DEFS := -DCL_ALLOC_HEADER='"fault_alloc.h"' -I$(TESTDIR)

$(BUILD)/test/gt_%.o: $(SRCDIR)/%.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) $(GT_DEFS) -MMD -MP $(ALL_CFLAGS) -c -o $@ $<

$(BUILD)/test/group_test: $(TESTDIR)/group_test.c $(TESTDIR)/fake_iterator.c $(TESTDIR)/fault_alloc.c \
        $(BUILD)/test/gt_list.o $(BUILD)/test/gt_process_group.o $(BUILD)/util.o $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) -I$(TESTDIR) -UNDEBUG $(ALL_CFLAGS) -o $@ $(filter %.c %.o,$^) $(LDFLAGS)

$(BUILD)/test/busy: $(TESTDIR)/busy.c $(BUILD)/.flags
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -o $@ $< $(LDFLAGS) -lpthread

ifneq ($(MAN_FILE),)
$(MAN_FILE): $(BIN)
	$(HELP2MAN) -n "$(MAN_NAME)" -s 1 -S "$(MAN_SOURCE)" -N ./$(BIN) > $@
endif

man: $(MAN_FILE)
	@test -n "$(MAN_FILE)" || echo "man: $(HELP2MAN) not found, man page skipped" >&2

# "test" is also a directory name, so it must stay phony.
check test: all $(TEST_BINS)
	BUILD=$(BUILD) sh $(TESTDIR)/run_tests.sh

strip: all
	$(STRIP) $(BIN)
	@cp -f $(BIN) $(COMPAT_BIN)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(NAME)
ifneq ($(MAN_FILE),)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -m 644 $(MAN_FILE) $(DESTDIR)$(MANDIR)/$(NAME).1
endif

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
	rm -rf build $(COMPAT_BIN) $(COMPAT_BIN).1

FORCE:

-include $(wildcard $(BUILD)/*.d $(BUILD)/test/*.d)
