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
#   make test-platforms -- the suite under each QPA in TEST_PLATFORMS
#   make coverage F=x -- line coverage for src/**/x.cpp
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
# src/*.h too: an internal header at the top of src/ was invisible to this
# list, which is how the gap below was found.
HEADERS = $(wildcard include/qtty/*.h src/*.h src/*/*.h src/*/*/*.h)
PROFILES = qtty.pro qtty.pri src/src.pro \
           tool/inspect/inspect.pro tool/replay/replay.pro \
           tool/negotiate/negotiate.pro example/chat/chat.pro
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
NEGOTIATE = $(BUILD_DIR)/tool/negotiate/qtty-negotiate
EXAMPLE  = $(BUILD_DIR)/example/chat/chat
TEST_BIN = $(TEST_BUILD_DIR)/qtty-tests

.DEFAULT_GOAL := all

# -----------------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------------

all: $(LIB)

# The header list is a prerequisite, and that is not belt-and-braces.
#
# qmake does NOT emit -MMD dependency files. It writes a STATIC dependency
# list into the generated Makefile, scanned once when qmake ran:
#
#     cell_buffer.o: ../../src/core/cell_buffer.cpp ../../include/qtty/cell.h
#
# That tracks headers correctly and goes stale the moment one is ADDED, since
# a header that did not exist at qmake time is in nobody's list. Measured: a
# new internal header was edited, `make` reported success, no object was
# recompiled, and the test binary that ran was the previous one -- which
# reports the previous answer, and a sabotage that changes nothing looks
# exactly like a check that cannot fail.
#
# Depending on $(HEADERS) re-runs qmake when any header changes, which costs a
# second and regenerates the snapshot. The alternative -- trusting a list
# captured at configure time -- is the class of fault build-and-commit.md
# calls load-bearing precisely because it produces a wrong answer rather than
# an error.
# The generated Makefile in each SUBDIR, named rather than found: every one of
# these paths is derived from a .pro file already listed in PROFILES, so the
# list cannot drift from the one qmake recurses into and no wildcard decides
# what gets removed.
#
# They are removed because qmake's subdirs template recurses with
#
#     cd src/ && ( test -e Makefile || qmake -o Makefile ... ) && make -f Makefile
#
# and that guard means a sub-Makefile is generated ONCE. Re-running the
# top-level qmake regenerates the top-level Makefile and leaves every
# sub-Makefile exactly as it was -- carrying the dependency scan taken the
# first time it was configured.
#
# So the mitigation one rule down, depending on $(HEADERS) so that qmake
# re-runs, does not reach the objects. Measured here, 2026-08-31: an #include
# of src/cell_geometry.h was added to src/render/cell_paint.cpp, `make test`
# reported success, and cell_paint.o was not rebuilt -- its dependency list had
# been written before the include existed. A sabotage of the header then failed
# to change the binary, 27 checks in other files went red and the one aimed at
# the sabotaged code passed, which reads exactly like a test that does not
# discriminate. It took `touch`ing the .cpp to find out otherwise.
#
# That is section 9.5's fault reached by a second door: not a header that is
# new, but an include that is. Removing them costs one qmake run per subdir on
# the configure step and nothing per build.
SUBDIR_MAKEFILES = $(addsuffix Makefile, \
                     $(addprefix $(BUILD_DIR)/, \
                       $(dir $(filter-out qtty.pro qtty.pri,$(PROFILES)))))

$(BUILD_DIR)/Makefile: $(PROFILES) VERSION $(HEADERS)
	mkdir -p $(BUILD_DIR)
	rm -f $(SUBDIR_MAKEFILES)
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
# one the library itself runs under and needs no display. Overridable, because
# a single platform is a single configuration: several faults in this tree hid
# in offscreen's particulars rather than in the code -- activePopupWidget() is
# permanently null there, no window ever activates, and a caret paints only
# under a selection. `make test-platforms` runs the suite under a second one.
TEST_PLATFORM ?= offscreen
TEST_ENV = QTTY_QPA_PLATFORM=$(TEST_PLATFORM) $(TEST_CRASH_ENV)

# Platforms the suite is expected to pass under, and the honest answer today
# is one. `minimal` was tried and cannot host the suite: it ships no font
# database, so DejaVu Sans Mono resolves to '' and grid_font_problem() refuses
# at startup -- correctly, since a grid needs integral metrics and there is no
# font to measure. `vnc` would open a listening socket and `linuxfb` writes to
# the console framebuffer, neither of which a test target should do by itself.
#
# That leaves `xcb` as the real second configuration. The whole suite passes
# under it, and getting there took one fix: the platform theme sets
# SH_DialogButtonBox_ButtonsHaveIcons, which a QProxyStyle passes through, so
# a dialog reserved width for an icon no cell renderer can draw and its button
# row moved two cells. GridStyle pins that hint now.
#
# It is deliberately not the default anyway, because it needs a display and
# puts windows on it. Run it when there is somebody to watch:
#
#     make test-platforms TEST_PLATFORMS="offscreen xcb"
TEST_PLATFORMS ?= offscreen

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
# Same reason as the library's rule above: qmake's dependency snapshot is
# taken once, and a header added afterwards is in no object's list.
$(TEST_BUILD_DIR)/Makefile: $(TEST_PROFILES) VERSION $(HEADERS)
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

# A platform theme this suite must be IMMUNE to. prepare_environment() pins
# QT_QPA_PLATFORMTHEME for the same reason it pins the platform, and that pin
# is one line somebody can delete; this is the only thing that would notice.
# Without it a desktop supplied per-class fonts that are not fixed pitch, and
# every column a widget computed was wrong.
#
# The plugin must exist or the run proves nothing -- an unknown theme name is
# ignored silently, which looks exactly like a pin that works.
TEST_HOSTILE_THEME ?= gtk3
QT_PLUGIN_PATH_FOR_CHECK = $(shell $(QMAKE) -query QT_INSTALL_PLUGINS 2>/dev/null)

# The same suite under each platform in TEST_PLATFORMS, then once more with a
# hostile theme in the environment. A pass under one platform says the code is
# right for that platform's assumptions and nothing more; this is what makes an
# assumption visible instead of load-bearing.
test-platforms: tests-build
	@failed=0; ran=0; \
	for platform in $(TEST_PLATFORMS); do \
		ran=$$((ran + 1)); \
		echo "--- $(TEST_BIN) on $$platform"; \
		QTTY_QPA_PLATFORM=$$platform $(TEST_CRASH_ENV) \
			timeout $(TEST_TIMEOUT) $(TEST_BIN) > /dev/null 2>&1 \
			&& echo "    ok" || { echo "    FAILED"; failed=$$((failed + 1)); }; \
	done; \
	plugin="$(QT_PLUGIN_PATH_FOR_CHECK)/platformthemes/libq$(TEST_HOSTILE_THEME).so"; \
	hostile="QT_SCALE_FACTOR=2 QT_SCREEN_SCALE_FACTORS=2"; \
	if [ -f "$$plugin" ]; then \
		hostile="$$hostile QT_QPA_PLATFORMTHEME=$(TEST_HOSTILE_THEME)"; \
	else \
		echo "    note: $$plugin is absent, so the theme half of the hostile" >&2; \
		echo "          environment is not applied and only scaling is tested." >&2; \
	fi; \
	echo "--- $(TEST_BIN) with a hostile environment: $$hostile"; \
	env $$hostile $(TEST_CRASH_ENV) \
		timeout $(TEST_TIMEOUT) $(TEST_BIN) > /dev/null 2>&1 \
		&& echo "    ok (the pins absorbed it)" \
		|| { echo "    FAILED"; failed=$$((failed + 1)); }; \
	echo "test-platforms: $$ran platform(s) + 1 hostile environment, $$failed failed"; \
	if [ "$$ran" -eq 0 ]; then \
		echo "test-platforms: TEST_PLATFORMS is empty, so the suite ran under" >&2; \
		echo "                no platform at all. The hostile-environment run" >&2; \
		echo "                above still happened, which is what makes this" >&2; \
		echo "                worth refusing: it prints a green summary for a" >&2; \
		echo "                target whose whole subject was skipped." >&2; \
		exit 1; \
	fi; \
	[ "$$failed" -eq 0 ]

# Line coverage for one source, measured rather than asserted -- and measured
# for THAT SOURCE, which is not what gcov's summary reports.
#
# `gcov -n <file>.cpp` prints a percentage for the translation unit, and a
# translation unit includes every header inlined into it. Adding one
# QHash<int,int> dropped term_caps.cpp from "100.00% of 150" to "99.44% of
# 177" without a line of its own going uncovered: the shortfall was Qt's
# hash internals, instantiated into the object and attributed to the file.
# A number that moves when a header is included is not a number about the
# file, and chasing it would have meant writing tests for QHashPrivate.
#
# The per-file .gcov listing carries only the file's own lines, so that is
# what is counted here, and every uncovered line is printed rather than
# summarised -- a percentage says how much and never which. Built into its
# own tree so an instrumented object never reaches a normal build, and named
# explicitly so `make coverage` cannot be the thing that quietly slows
# everything else down.
#
#     make coverage F=term_caps
#
# It exists because "fully covered" is a claim with a short shelf life: the
# capability parser was written to be at 100% and the only thing that will
# notice a new uncovered branch is a command somebody can run.
COV_DIR = build-cov
coverage:
	@test -n "$(F)" || { echo "coverage: name the file, e.g. make coverage F=term_caps" >&2; exit 1; }
	$(MAKE) test BUILD_DIR=$(COV_DIR) \
		QMAKE_CONFIG='CONFIG+=release CONFIG-=debug \
		              QMAKE_CXXFLAGS+=--coverage QMAKE_LFLAGS+=--coverage' >/dev/null
	@( cd $(COV_DIR)/src && gcov $(F).cpp >/dev/null 2>&1 )
	@test -f $(COV_DIR)/src/$(F).cpp.gcov \
		|| { echo "coverage: no data for $(F)" >&2; exit 1; }
	@awk -F: '$$1 ~ /^ *[0-9]+$$/ { ex++ } $$1 ~ /^ *#####/ { un++; print "  uncovered: " $$2 ": " $$3 } \
	     END { t = ex + un; \
	           printf "%s: %d of %d lines, %.2f%%\n", "$(F).cpp", ex, t, t ? 100.0 * ex / t : 0 } ' \
	    $(COV_DIR)/src/$(F).cpp.gcov

# Rewrite a snapshot fixture after a reviewed change: make record R=render
record: tests-build
	@test -n "$(R)" || { echo "record: name the fixture, e.g. make record R=render" >&2; exit 1; }
	$(TEST_ENV) $(TEST_BIN) --record $(R)

# version-check joins these because a gate nobody runs is not a gate: it was
# reachable only by typing its name, so the version consistency it enforces
# and the copyright line it now checks were both unguarded in practice. It is
# pure sed and grep and costs nothing.
check: style layout version-check test

# -----------------------------------------------------------------------------
# Gates
# -----------------------------------------------------------------------------

style: style-source style-docs

style-source:
	python3 tool/style_gate.py check

# design.md section 7's enforcement rule: shared view code must not hardcode
# margins, spacing or pixel sizes. The document scopes it to an application's
# src/ui/shared/, which is a directory this repository does not have -- qtty is
# the library. What it does have is the UI it ships: the example, which exists
# to show one view codebase serving both targets, and the tools' own windows.
# Those are the shared view code here, so those are what the gate reads.
#
# NOT test/. A fixture is built at an exact size on purpose, so that the cell
# arithmetic a check asserts is arithmetic and not a layout's opinion.
LAYOUT_SRC = $(wildcard example/*/*.h example/*/*.cpp tool/*/*.h tool/*/*.cpp)

#
# The wildcard is the exposure, not the gate: a moved directory or a renamed
# file makes it match nothing, `layout_gate.py` with no paths prints its usage,
# and a `make style` that checked nothing looks exactly like one that passed.
# So the list is required to be non-empty here, and the gate reports how many
# files and call sites it actually judged.
layout:
	@test -n "$(strip $(LAYOUT_SRC))" || { \
		echo "layout: LAYOUT_SRC matched no files, so the gate would read" >&2; \
		echo "        nothing and exit 0 -- that reads like a pass and is" >&2; \
		echo "        not one. Check the wildcard against the tree." >&2; \
		exit 1; \
	}
	python3 tool/layout_gate.py $(LAYOUT_SRC)

# project.md is authoritative, so it is held to the tree: a heading that
# appears twice means whichever one you find, the other is the one with the
# answer.
style-docs:
	python3 tool/style_gate.py docs

# The commit-msg hook lives in the tree so it is reviewable, survives a clone,
# and can be kept in sync. .git/hooks is untracked, so a hook that exists only
# there enforces a rule nobody can see and vanishes silently on a fresh clone.
#
# `git rev-parse --git-common-dir` rather than `test -d .git`, and the
# difference is not pedantry: in a LINKED WORKTREE -- which this tree has two
# of, used for isolated agent work -- `.git` is a regular FILE holding a
# pointer, so the directory test is false and this target refused in exactly
# the trees the work is being done in.
#
# Repairing only the guard would be worse than the refusal. `[ -e .git ]` and
# `rev-parse --is-inside-work-tree` both pass in a worktree, and `.git/hooks`
# there is not a directory -- measured in
# .claude/worktrees/agent-a28b25cb0976d1f79 -- so a guard fixed that way
# installs into a path that does not exist and says it succeeded.
# `--git-common-dir` answers with where hooks actually live: they are shared
# across a repository's worktrees and belong to the main `.git`.
hooks:
	@command -v git >/dev/null 2>&1 || { \
		echo "hooks: git is not installed, so there is nowhere to" >&2; \
		echo "       install to." >&2; \
		exit 1; \
	}
	@dir=$$(git rev-parse --git-common-dir 2>/dev/null); \
	if [ -z "$$dir" ]; then \
		echo "hooks: not a git repository, so there is nowhere to" >&2; \
		echo "       install to." >&2; \
		exit 1; \
	fi; \
	mkdir -p "$$dir/hooks"; \
	install -m 0755 tool/hooks/commit-msg "$$dir/hooks/commit-msg"; \
	echo "hooks: commit-msg installed into $$dir/hooks/"

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
	header=$$(sed -n 's/.*version_string = "\([^"]*\)".*/\1/p' include/qtty/version.h); \
	if [ "$$file" != "$$header" ]; then \
		echo "version-check: VERSION says $$file but" >&2; \
		echo "               include/qtty/version.h says $$header" >&2; \
		exit 1; \
	fi; \
	major=$$(sed -n 's/.*version_major = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	minor=$$(sed -n 's/.*version_minor = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	patch=$$(sed -n 's/.*version_patch = \([0-9]*\).*/\1/p' include/qtty/version.h); \
	if [ "$$major.$$minor.$$patch" != "$$file" ]; then \
		echo "version-check: VERSION says $$file but version.h's" >&2; \
		echo "               components are $$major.$$minor.$$patch" >&2; \
		exit 1; \
	fi; \
	holder=$$(sed -n 's/^    "\(Copyright (C).*\)";$$/\1/p' include/qtty/version.h); \
	if [ -z "$$holder" ]; then \
		echo "version-check: include/qtty/version.h states no copyright line" >&2; \
		exit 1; \
	fi; \
	if ! grep -qF "$$holder" README.md; then \
		echo "version-check: README.md does not carry the copyright line" >&2; \
		echo "               version.h states: $$holder" >&2; \
		exit 1; \
	fi; \
	echo "version-check: $$file, in step, attributed to $$holder"

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

.PHONY: all test test-platforms tests-build coverage record check style style-source style-docs layout hooks \
        version-check run install uninstall clean veryclean distclean help FORCE
