# =============================================================================
# Top-level Makefile -- qtty
#
# PURPOSE
#   The single entry point for the tree, and the same one every sibling
#   project presents. qtty is built by qmake, because moc does not fit
#   hand-written pattern rules; everything else here is plain make. This file
#   is the wrapper that makes the two look like one interface -- the split
#   beerssh, hydra and bbq-predictor already use.
#
#   It also exists to keep the build out of the source tree. Before it, every
#   .pro file said DESTDIR = $$PWD/../lib, so the static library was written
#   into a tracked path whatever qmake was told; lib/libqtty.a was committed
#   and each build rewrote it. qtty.pri derives the library directory with
#   $$shadowed(), and this file gives qmake somewhere to shadow into.
#
# TARGETS
#   make              -- build the library, the tools and the example
#   make test         -- build and run the test suite
#   make check        -- style + test; what must pass before committing
#   make style        -- the shared source gate and the project.md checks
#   make hooks        -- install tool/hooks/commit-msg into .git/hooks
#   make version-check -- VERSION, qtty.pri and version.h still agree
#   make run          -- build and run the chat example in TUI mode
#   make install      -- install headers, library, tools and the example
#   make uninstall    -- remove what install put there
#   make clean        -- remove build intermediates, leaving the tree alone
#   make veryclean    -- clean, plus the build directories
#   make distclean    -- veryclean, plus qmake's own droppings
#   make help         -- print this list
#
# BUILD FLAGS
#   DEBUG=1      -- a debug build (-Og), instead of the -Os release default
#   SANITIZE=1   -- address and undefined-behaviour sanitizers
#   BUILD_DIR=x  -- where the build lands (default `build`)
#   CXX=x QMAKE=x -- toolchain override
#
#   DEBUG and SANITIZE are deliberately NOT given `?=` defaults: they are
#   tested with ifdef, and `DEBUG ?= 0` would make them permanently set.
#   Everything else is `?=` so that the command line and the environment win.
# =============================================================================

CXX   ?= g++
QMAKE ?= $(shell command -v qmake6 2>/dev/null || command -v qmake 2>/dev/null || echo qmake6)

TARGET  = qtty
VERSION ?= $(shell cat VERSION)

BUILD_DIR      ?= build
TEST_BUILD_DIR ?= $(BUILD_DIR)-test
DEB_DIR        ?= $(BUILD_DIR)/deb

PREFIX  ?= /usr/local
DESTDIR ?=

SOURCES = $(wildcard src/*.cpp src/*/*.cpp src/*/*/*.cpp)
HEADERS = $(wildcard include/qtty/*.h src/*/*.h src/*/*/*.h)
PROFILES = qtty.pro qtty.pri src/src.pro \
           tool/inspect/inspect.pro tool/replay/replay.pro example/chat/chat.pro
TEST_PROFILES = test/test.pro qtty.pri

ifdef DEBUG
    QMAKE_CONFIG = CONFIG+=debug CONFIG-=release
else
    QMAKE_CONFIG = CONFIG+=release CONFIG-=debug
endif
ifdef SANITIZE
    QMAKE_CONFIG += QMAKE_CXXFLAGS+=-fsanitize=address,undefined \
                    QMAKE_CXXFLAGS+=-fno-omit-frame-pointer \
                    QMAKE_LFLAGS+=-fsanitize=address,undefined
endif

# The library, the two tools and the example, at the paths qmake's subdirs
# template puts them under a shadow build.
LIB      = $(BUILD_DIR)/lib/libqtty.a
INSPECT  = $(BUILD_DIR)/tool/inspect/qtty-inspect
REPLAY   = $(BUILD_DIR)/tool/replay/qtty-replay
EXAMPLE  = $(BUILD_DIR)/example/chat/chat
TEST_BIN = $(TEST_BUILD_DIR)/qtty-tests

.DEFAULT_GOAL := all

# -----------------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------------

all: $(LIB)

$(BUILD_DIR)/Makefile: $(PROFILES) VERSION
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(QMAKE) $(CURDIR)/qtty.pro $(QMAKE_CONFIG) QMAKE_CXX=$(CXX)

# FORCE, and it is load-bearing rather than belt-and-braces. A rule listing
# $(SOURCES) as its prerequisites is a claim that this file knows what the
# sub-make depends on, and it does not: only the generated Makefile knows,
# and it tracks headers through -MMD that no wildcard here can see. The first
# version of this rule was keyed on $(SOURCES) alone, which is src/ only --
# so editing a test file left $(LIB) up to date, make printed "Nothing to be
# done for 'all'", the sub-make never ran, and the test binary that got run
# was the previous one. A stale binary reports the previous answer, which is
# indistinguishable from the new code being correct. Recursing every time
# costs a no-op sub-make and does not force a relink.
.PHONY: FORCE
FORCE:

$(LIB): $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR)

# -----------------------------------------------------------------------------
# Test
#
# `make check` is style plus test, which is GNU's meaning of the name and what
# the sibling projects do. Note qmake's generated Makefile also offers a
# `check` inside BUILD_DIR (CONFIG testcase); this one is the tree's.
# -----------------------------------------------------------------------------

# Precautionary, and inert today -- said plainly because an inert guard with a
# dramatic comment is worse than none. QtTest runs `gdb --batch --pid <self>`
# on a fatal signal, and a traced process that stops is neither reaped nor
# killable with SIGTERM; five of them held 15 GB resident in a sibling project.
# This suite links no QtTest and uses none, so the variables currently guard
# nothing. They are set anyway because the cost is zero and the guard becomes
# load-bearing the day a suite adds QtTest -- which is the day nobody would
# think to add them.
ifdef QTTY_TEST_STACK_DUMP
    TEST_CRASH_ENV =
else
    TEST_CRASH_ENV = QTEST_DISABLE_STACK_DUMP=1 QTEST_DISABLE_CORE_DUMP=1
endif

# The suite renders widgets, so it needs a platform plugin; offscreen is the
# one the library itself runs under and needs no display.
TEST_ENV = QT_QPA_PLATFORM=offscreen $(TEST_CRASH_ENV)

# This timeout is convenience, not the bound. The suite carries its own
# wall-clock limit in test/main.cpp, because a limit that lives only here
# protects `make test` and protects nobody debugging -- and running the binary
# directly is what debugging means. That is not hypothetical here: a suite
# waiting on an event loop that never started hung until it was killed from
# another terminal, and it was being run by hand at the time to find out why.
TEST_TIMEOUT ?= 300

# test/ is not a SUBDIR of qtty.pro, so the default build cannot produce a
# test binary at all -- which is the rule in build-and-commit.md, and also
# removes the whole class of "did that get rebuilt?" from the default path.
# It gets its own qmake run and its own build directory, and is told where
# the library is because $$shadowed() cannot work it out from over here.
$(TEST_BUILD_DIR)/Makefile: $(TEST_PROFILES) VERSION
	mkdir -p $(TEST_BUILD_DIR)
	cd $(TEST_BUILD_DIR) && $(QMAKE) $(CURDIR)/test/test.pro $(QMAKE_CONFIG) \
	        QMAKE_CXX=$(CXX) QTTY_LIB_DIR=$(CURDIR)/$(BUILD_DIR)/lib

tests-build: $(LIB) $(TEST_BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(TEST_BUILD_DIR)

test: tests-build
	@ran=0; failed=0; \
	for binary in $(TEST_BIN); do \
		[ -x "$$binary" ] && [ -f "$$binary" ] || continue; \
		ran=$$((ran + 1)); \
		echo "--- $$binary"; \
		$(TEST_ENV) timeout $(TEST_TIMEOUT) "$$binary" || failed=$$((failed + 1)); \
	done; \
	if [ "$$ran" -eq 0 ]; then \
		echo "test: no test binary was found at $(TEST_BIN)." >&2; \
		echo "test: a run over zero binaries exits 0 and reads exactly" >&2; \
		echo "      like a pass, so this is a failure." >&2; \
		exit 1; \
	fi; \
	echo "test: $$ran binary(ies), $$failed failed"; \
	[ "$$failed" -eq 0 ]

# Rewrite a snapshot fixture after a reviewed change: make record R=render
record: tests-build
	@test -n "$(R)" || { echo "record: name the fixture, e.g. make record R=render" >&2; exit 1; }
	$(TEST_ENV) $(TEST_BIN) --record $(R)

check: style test

# -----------------------------------------------------------------------------
# Gates
# -----------------------------------------------------------------------------

style: style-source style-docs

style-source:
	python3 tool/style_gate.py check

# project.md is authoritative, so it is held to the tree: a heading that
# appears twice means whichever one you find, the other is the one with the
# answer.
style-docs:
	python3 tool/style_gate.py docs

# The commit-msg hook lives in the tree so it is reviewable, survives a clone,
# and can be kept in sync. .git/hooks is untracked, so a hook that exists only
# there enforces a rule nobody can see and vanishes silently on a fresh clone.
hooks:
	@test -d .git || { echo "hooks: not a git repository" >&2; exit 1; }
	@install -m 0755 tool/hooks/commit-msg .git/hooks/commit-msg
	@echo "hooks: commit-msg installed from tool/hooks/"

# One version, in one file. qtty.pri must still read VERSION rather than state
# a number, and include/qtty/version.h -- which is a public header and cannot
# read a file at compile time -- must agree with it.
version-check:
	@file=$$(cat VERSION); \
	if ! grep -q 'VERSION = \$$\$$cat(\$$\$$PWD/VERSION' qtty.pri; then \
		echo "version-check: qtty.pri states a version instead of reading" >&2; \
		echo "               VERSION; the two will drift" >&2; \
		exit 1; \
	fi; \
	header=$$(sed -n 's/.*versionString = "\([^"]*\)".*/\1/p' include/qtty/version.h); \
	if [ "$$file" != "$$header" ]; then \
		echo "version-check: VERSION says $$file but" >&2; \
		echo "               include/qtty/version.h says $$header" >&2; \
		exit 1; \
	fi; \
	major=$$(sed -n 's/.*versionMajor = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	minor=$$(sed -n 's/.*versionMinor = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	patch=$$(sed -n 's/.*versionPatch = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	if [ "$$major.$$minor.$$patch" != "$$file" ]; then \
		echo "version-check: VERSION says $$file but version.h's" >&2; \
		echo "               components are $$major.$$minor.$$patch" >&2; \
		exit 1; \
	fi; \
	echo "version-check: $$file, in step"

# -----------------------------------------------------------------------------
# Run and install
# -----------------------------------------------------------------------------

run: $(LIB)
	$(EXAMPLE) --tui

install: $(LIB)
	install -d $(DESTDIR)$(PREFIX)/include/qtty
	install -m 0644 include/qtty/*.h $(DESTDIR)$(PREFIX)/include/qtty/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(INSPECT) $(REPLAY) $(DESTDIR)$(PREFIX)/bin/

# Named, not globbed: a clean or an uninstall is the one target everybody runs
# without reading it.
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/qtty-inspect
	rm -f $(DESTDIR)$(PREFIX)/bin/qtty-replay
	rm -f $(DESTDIR)$(PREFIX)/lib/libqtty.a
	rm -rf $(DESTDIR)$(PREFIX)/include/qtty

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------

clean:
	@if [ -f $(BUILD_DIR)/Makefile ]; then $(MAKE) -C $(BUILD_DIR) clean; fi
	@if [ -f $(TEST_BUILD_DIR)/Makefile ]; then $(MAKE) -C $(TEST_BUILD_DIR) clean; fi

# A wildcard is legitimate only where the names are not knowable, and then the
# directory has to be vouched for instead: it must be non-empty, relative, and
# incapable of resolving to a source tree, a home directory or /. An unset
# BUILD_DIR in `rm -rf $(BUILD_DIR)` is exactly how a clean target eats
# something it should not.
define qtty_remove_tree
	@for dir in $(1); do \
		test -n "$$dir" || { echo "veryclean: refusing to remove an empty path" >&2; exit 1; }; \
		case "$$dir" in \
			/*) echo "veryclean: refusing the absolute path $$dir" >&2; exit 1 ;; \
			*..*) echo "veryclean: refusing $$dir -- it escapes the tree" >&2; exit 1 ;; \
			.|./) echo "veryclean: refusing the working directory" >&2; exit 1 ;; \
		esac; \
		if [ -d "$$dir" ]; then echo "veryclean: removing $$dir"; rm -rf "$$dir"; fi; \
	done
endef

veryclean: clean
	$(call qtty_remove_tree,$(BUILD_DIR) $(TEST_BUILD_DIR))

# What this project's own tooling wrote, each named. It deliberately does NOT
# sweep the tree for *~, *.swp or *.orig: a swap file belongs to an editor that
# may still have the file open, a .orig to a merge somebody may be part-way
# through, and `find .` from here walks .git as well. `git clean -xdn` lists
# that class, and removing it is a person's call, not the build system's.
distclean: veryclean
	@for f in .qmake.stash tool/__pycache__; do \
		if [ -e "$$f" ]; then echo "distclean: removing $$f"; rm -rf "$$f"; fi; \
	done

help:
	@sed -n '/^# TARGETS/,/^#$$/p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'

.PHONY: all test tests-build record check style style-source style-docs hooks \
        version-check run install uninstall clean veryclean distclean help FORCE
