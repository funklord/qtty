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
#   make test-sanitize -- the suite under ASan, UBSan and the leak detector
#   make test-valgrind -- the suite under valgrind memcheck
#   make test-consume  -- build a program against the installed library
#   make test-negotiate -- ask a second terminal what qtty concludes
#   make test-screen  -- what a real terminal DRAWS, captured and counted
#   make test-tools   -- the shipped tools and the example, RUN not just built
#   make test-install -- install into a scratch root and check what landed
#   make count-check  -- project.md's stated check count against the real one
#   make sabotage     -- break the code on purpose; every check must go red
#   make coverage F=x -- line coverage for src/**/x.cpp
#   make check        -- style + test; what must pass before committing
#   make style        -- the shared source gate and the project.md checks
#   make hooks        -- install tool/hooks/commit-msg into .git/hooks
#   make version-check -- VERSION, qtty.pri and version.h still agree
#   make run          -- build and run the chat example in TUI mode
#   make install      -- headers, the library, qtty-inspect and qtty-replay
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
# It stays out of TEST_PLATFORMS because that list is platforms run directly,
# and xcb needs a display. It is no longer a configuration nobody runs: the
# recipe below runs it under Xvfb when Xvfb is installed, which is the whole
# of what "needs a display and puts windows on it" was an objection to. Run it
# against a real one when there is somebody to watch:
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

# The same suite under each platform in TEST_PLATFORMS, then under xcb on a
# virtual display, then once more with a hostile theme in the environment. A
# pass under one platform says the code is right for that platform's
# assumptions and nothing more; this is what makes an assumption visible
# instead of load-bearing.
#
# The xcb arm is here rather than in TEST_PLATFORMS because it needs a display
# and Xvfb supplies one -- no watcher, nobody's screen. It earned its place the
# first time it ran: a check that forks had a child creating widgets on the
# parent's X connection, which broke the connection and took the two suites
# after it with it, and offscreen could not see any of that.
#
# QTEST_DISABLE_STACK_DUMP because QtTest forks gdb on a fatal signal, and a
# crash handler that attaches a debugger is how this workspace once lost 15 GB
# of resident memory to a test run nobody was watching.
#
# `minimal` is here as the one configuration where PASSING means the program
# stopped. It ships no font database, so the grid font resolves to nothing and
# there is no cell to derive from it -- and the library's job there is to say
# so and stop, not to draw a screen it cannot get right. This file has
# described that in a comment since the platform was first tried; the arm
# below asserts it.
#
# Both halves, because either alone passes for the wrong reason: the run must
# FAIL, and its message must name what was actually tested. A crash with no
# explanation satisfies the first; a clean start that happens to print the
# sentence satisfies the second.
#
# The pattern anchors on the REFUSAL's own words rather than on the sentence
# alone, and that is not fussiness -- it is what makes the arm discriminate.
# The same sentence is printed twice, once as setup()'s warning and once
# inside the refusal, so a pattern matching either passed a build where the
# refusal had been broken and only the warning survived. Measured: sabotaging
# grid_font_problem()'s empty-family branch left the arm green.
#
# Each arm also COUNTS what it ran, and the first configuration sets the
# number the rest must match. Reading only the exit status is right -- that is
# the status channel, and grepping a log for success words is how a check goes
# quiet -- but a status alone cannot tell a configuration that ran the whole
# suite from one where a fixture bailed early and took a block of checks with
# it. Both print `ok`. `count-check` holds the number for the offscreen run
# and nothing held it for the other configurations, so this is that gate
# arriving where the suite is actually re-run under something different.
#
# Counted from stdout with stderr dropped, because offscreen's stderr
# interleaves and cuts those lines in half -- a counter over the merged streams
# undercounts, which is the failure this rule exists to catch, manufactured by
# the rule itself. The `minimal` arm is deliberately outside it: that one must
# refuse before running anything, so its check count is zero by design and it
# carries its own assertion.
#
# PASS, FAIL and SKIP together, not PASS alone, and the difference decides
# whether this gate is usable. Six checks in the suite stand down rather than
# assert -- no temporary directory, no proportional font resolved, a user who
# can read a mode-000 file -- and font resolution in particular can differ
# between one QPA plugin and another, so a PASS-only count would go red for a
# check that correctly declined. The question here is "was this check site
# reached", and a SKIP answers it as well as a PASS does. A block that bailed
# early loses both, which is the thing being caught.
# What a terminal DRAWS, rather than what qtty emitted. Every graphics tier
# here is exercised by its encoder, which leaves the question a user actually
# has -- is the picture right -- answered by nobody. project.md said several
# times that it could not be answered on this machine; it can, and the note
# was about what the suite reaches for rather than about the machine.
#
# Kept out of `check` deliberately. It starts a GUI process under a virtual
# display and takes seconds rather than milliseconds, and `check` is the thing
# run before every commit. It skips rather than fails where kitty, Xvfb,
# ImageMagick or PIL are missing, for the reason test-platforms skips without
# xvfb-run: a machine without a terminal emulator is not a machine with a
# broken renderer.
# Depends on the library, for the reason build-and-commit.md gives about
# never judging a test from a binary the build did not rebuild: this one
# compiles a probe AGAINST that library, so a stale one is a screen check of
# code nobody is running. Measured -- with an out-of-date build/ it reported
# left=220 against a prediction of 198, which is the old font-sized path,
# correctly and confusingly.
test-screen: $(LIB)
	@./tool/screen-check $(BUILD_DIR)

# The negotiation the screen check depends on, asked of a second terminal.
# Out of `check` for the same reason test-screen is: it starts terminals
# under a virtual display and takes a minute, where `check` takes
# milliseconds and runs before every commit.
test-negotiate: $(NEGOTIATE)
	@./tool/negotiate-check $(BUILD_DIR)

# Every check in the suite, put to the question it cannot answer about
# itself: would it have SPOKEN if the code had been wrong. tool/sabotage.toml
# names deliberate breakages and, for each, the check that must go red.
#
# It is a target rather than a convention because a convention is obeyed
# when somebody remembers. Every entry in that spec was performed by hand
# once, watched failing, and then would have been forgotten -- and a check
# that quietly stops discriminating looks exactly like a clean tree, which
# is the one thing no green run can tell you.
#
# Out of `check` for the plainest reason: it rebuilds and re-runs the suite
# once per entry, so it costs minutes where `check` costs milliseconds, and
# `check` runs before every commit. Run it after touching a check, after
# touching the code a check defends, and before believing a green suite that
# has been green for a long time.
#
# It refuses on a dirty src/, test/ or include/, and that is not tidiness:
# it edits source files and puts them back, and in a tree more than one
# session works in, the file it would restore may be somebody else's work in
# progress. --dirty-ok is there for the case where every line is yours.
sabotage:
	@python3 tool/sabotage.py $(SABOTAGE_ARGS)

# The adoption path: a program built against the INSTALLED library, with
# nothing from this tree. Out of `check` because it installs into a temporary
# prefix and compiles a Qt program, which takes seconds where `check` takes
# milliseconds -- and `check` is what runs before every commit.
test-consume:
	@./tool/consume-check

test-platforms: tests-build
	@failed=0; ran=0; expect=; \
	out=$(dir $(TEST_BIN))platform.out; \
	cnt() { grep -cE '^(PASS|FAIL|SKIP):' "$$1" || true; }; \
	agree() { \
		n=$$(cnt "$$1"); \
		if [ -z "$$expect" ]; then \
			if [ "$$n" -eq 0 ]; then \
				echo "    FAILED: no checks ran at all" >&2; \
				echo; return 1; \
			fi; \
			echo "$$n"; return 0; \
		fi; \
		if [ "$$n" != "$$expect" ]; then \
			echo "    FAILED: $$n checks, where the first configuration" >&2; \
			echo "            ran $$expect. A configuration that silently" >&2; \
			echo "            runs fewer is green on the exit status alone." >&2; \
			echo "$$expect"; return 1; \
		fi; \
		echo "$$expect"; \
	}; \
	for platform in $(TEST_PLATFORMS); do \
		ran=$$((ran + 1)); \
		echo "--- $(TEST_BIN) on $$platform"; \
		if QTTY_QPA_PLATFORM=$$platform $(TEST_CRASH_ENV) \
		     timeout $(TEST_TIMEOUT) $(TEST_BIN) > "$$out" 2>/dev/null; then \
			if expect=$$(agree "$$out"); \
			then echo "    ok ($$(cnt "$$out") checks)"; \
			else failed=$$((failed + 1)); fi; \
		else echo "    FAILED"; failed=$$((failed + 1)); fi; \
	done; \
	if command -v xvfb-run >/dev/null 2>&1; then \
		echo "--- $(TEST_BIN) on xcb, under Xvfb"; \
		ran=$$((ran + 1)); \
		if xvfb-run -a -s "-screen 0 1280x1024x24" \
		     env QTTY_QPA_PLATFORM=xcb QTEST_DISABLE_STACK_DUMP=1 \
		     timeout $(TEST_TIMEOUT) $(TEST_BIN) > "$$out" 2>/dev/null; then \
			if expect=$$(agree "$$out"); \
			then echo "    ok ($$(cnt "$$out") checks)"; \
			else failed=$$((failed + 1)); fi; \
		else echo "    FAILED"; failed=$$((failed + 1)); fi; \
	else \
		echo "    note: xvfb-run is absent, so the xcb configuration is not" >&2; \
		echo "          run and only $(TEST_PLATFORMS) was tested." >&2; \
	fi; \
	minimal="$(QT_PLUGIN_PATH_FOR_CHECK)/platforms/libqminimal.so"; \
	if [ -f "$$minimal" ]; then \
		echo "--- $(TEST_BIN) on minimal, which must REFUSE (the abort below is the point)"; \
		err="$(dir $(TEST_BIN))minimal.err"; \
		( QTTY_QPA_PLATFORM=minimal $(TEST_CRASH_ENV) \
			timeout $(TEST_TIMEOUT) $(TEST_BIN) >/dev/null 2>"$$err" \
		) 2>/dev/null; \
		minrc=$$?; \
		minout=$$(cat "$$err"); rm -f "$$err"; \
		case "$$minrc:$$minout" in \
		0:*) echo "    FAILED: it started, with no font database to start on"; \
		     failed=$$((failed + 1));; \
		*"the grid needs a font"*"resolved to no font at all"*) \
		     echo "    ok (refused, and said why)";; \
		*)   echo "    FAILED: it refused without naming the cause"; \
		     echo "    got: $$minout" >&2; failed=$$((failed + 1));; \
		esac; \
	else \
		echo "    note: $$minimal is absent, so the refusal is not tested." >&2; \
	fi; \
	plugin="$(QT_PLUGIN_PATH_FOR_CHECK)/platformthemes/libq$(TEST_HOSTILE_THEME).so"; \
	hostile="QT_SCALE_FACTOR=2 QT_SCREEN_SCALE_FACTORS=2"; \
	if [ -f "$$plugin" ]; then \
		hostile="$$hostile QT_QPA_PLATFORMTHEME=$(TEST_HOSTILE_THEME)"; \
	else \
		echo "    note: $$plugin is absent, so the theme half of the hostile" >&2; \
		echo "          environment is not applied and only scaling is tested." >&2; \
	fi; \
	echo "--- $(TEST_BIN) with a hostile environment: $$hostile"; \
	if env $$hostile $(TEST_CRASH_ENV) \
	     timeout $(TEST_TIMEOUT) $(TEST_BIN) > "$$out" 2>/dev/null; then \
		if expect=$$(agree "$$out"); \
		then echo "    ok, the pins absorbed it ($$(cnt "$$out") checks)"; \
		else failed=$$((failed + 1)); fi; \
	else echo "    FAILED"; failed=$$((failed + 1)); fi; \
	rm -f "$$out"; \
	echo "test-platforms: $$ran platform(s), 1 refusal and 1 hostile environment, $$failed failed"; \
	if [ "$$ran" -eq 0 ]; then \
		echo "test-platforms: TEST_PLATFORMS is empty, so the suite ran under" >&2; \
		echo "                no platform at all. The hostile-environment run" >&2; \
		echo "                above still happened, which is what makes this" >&2; \
		echo "                worth refusing: it prints a green summary for a" >&2; \
		echo "                target whose whole subject was skipped." >&2; \
		exit 1; \
	fi; \
	[ "$$failed" -eq 0 ]

# The suite under AddressSanitizer, UndefinedBehaviorSanitizer and the leak
# detector, in its own build directory so the ordinary one is not disturbed.
#
# Leaks are ON. The first run of this axis reported none at all -- not even
# Qt's usual noise -- and a detector left switched off is one nobody notices
# going quiet.
#
# The instrument was verified rather than trusted, because a clean sanitizer
# run over a binary that was never instrumented looks exactly like a real one:
# -fsanitize=address,undefined appears in the build log, the binary carries
# __asan symbols and links libasan, and a deliberate heap-buffer-overflow and
# a signed overflow were both caught by the same flags.
#
# The suite carries its own watchdog, and a sanitized run is about twice as
# slow -- 3.6 s against 1.8 s here -- so the raised limit is generous rather
# than necessary.
SAN_BUILD_DIR ?= build-san
DBG_BUILD_DIR ?= build-dbg

test-sanitize:
	@QTTY_TEST_TIMEOUT=900 ASAN_OPTIONS=detect_leaks=1 \
		$(MAKE) test BUILD_DIR=$(SAN_BUILD_DIR) SANITIZE=1

# What `make install` actually lays down, and what `make uninstall` takes back.
# Neither had ever been run by anything: a packaging target is exactly the kind
# that is exercised once by hand and then not again until it matters.
#
# The expected list is NAMED while the install rule GLOBS, and that polarity is
# deliberate. `install` copies `include/qtty/*.h`, so a new public header ships
# the moment it exists; naming them here means that becomes a failure to be
# waved through deliberately rather than a thing that happened.
#
# Everything happens under $(BUILD_DIR)/stage, which this target creates and
# removes -- a DESTDIR is the one variable in a packaging rule that must never
# be allowed to be empty, and the guard below is why.
INSTALLED_HEADERS = application.h \
	                  backend.h \
	                  cell.h \
	                  color.h \
	                  delegate.h \
	                  graphics.h \
	                  grid.h \
	                  null_backend.h \
	                  overlay.h \
	                  paint.h \
	                  qtty.h \
	                  runtime.h \
	                  testing.h \
	                  theme.h \
	                  version.h

INSTALLED_FILES = usr/bin/qtty-inspect usr/bin/qtty-replay usr/lib/libqtty.a \
                  usr/lib/pkgconfig/qtty.pc \
                  $(patsubst %,usr/include/qtty/%,$(INSTALLED_HEADERS))

test-install: $(LIB) $(INSPECT) $(REPLAY)
	@test -n "$(strip $(BUILD_DIR))" || { \
		echo "test-install: BUILD_DIR is empty, refusing to stage" >&2; exit 1; \
	}
	@stage="$(BUILD_DIR)/stage"; \
	rm -rf "$$stage"; \
	$(MAKE) --no-print-directory install DESTDIR="$$PWD/$$stage" PREFIX=/usr \
		> /dev/null || exit 1; \
	missing=0; \
	for f in $(INSTALLED_FILES); do \
		[ -s "$$stage/$$f" ] || { echo "    missing or empty: $$f"; missing=1; }; \
	done; \
	for f in usr/bin/qtty-inspect usr/bin/qtty-replay; do \
		[ -x "$$stage/$$f" ] || { echo "    not executable: $$f"; missing=1; }; \
	done; \
	for f in $$(cd "$$stage" && find . -type f | sed 's|^\./||'); do \
		case " $(INSTALLED_FILES) " in \
		*" $$f "*) ;; \
		*) echo "    installed but not named in the list: $$f"; missing=1;; \
		esac; \
	done; \
	$(MAKE) --no-print-directory uninstall DESTDIR="$$PWD/$$stage" PREFIX=/usr \
		> /dev/null || exit 1; \
	left=$$(cd "$$stage" && find . -type f | wc -l); \
	[ "$$left" -eq 0 ] || { \
		echo "    uninstall left $$left file(s) behind:"; \
		(cd "$$stage" && find . -type f) ; missing=1; \
	}; \
	rm -rf "$$stage"; \
	[ "$$missing" -eq 0 ] || { \
		echo "test-install: the packaging rules do not agree with the list" >&2; \
		exit 1; \
	}; \
	echo "test-install: $(words $(INSTALLED_FILES)) file(s) installed and removed"

# The three shipped tools and the example, run rather than merely built.
# `make install` puts all four in $$(PREFIX)/bin, and until now the only thing
# holding them to anything was the compiler: a tool that aborted at startup
# would have shipped, and the example -- which exists to show one view codebase
# serving both targets -- was never once seen to draw.
#
# Each assertion is on what the thing is FOR rather than on its exit status: a
# program that prints nothing and exits 0 satisfies a status check, which is
# this document's oldest complaint about gates.
#
# `--version` is checked against the VERSION file, which `version-check` cannot
# do: that target holds VERSION, qtty.pri and version.h to each other, and this
# is the only thing that asks the SHIPPED BINARY what it thinks it is.
#
# `--probes` down a pipe must not send the query. AnsiBackend states the rule --
# "down a pipe there is nobody to answer, and the query would be written into
# whatever is reading" -- and this tool was breaking it, so `--probes >
# report.txt` filled the report with control bytes and then waited 200 ms for
# an answer that cannot come.
#
# The replay snapshot arm asserts BOTH halves of why that command exists: a
# selection must change the snapshot, and must NOT change the text frame.
# `frame` prints glyphs, so `ctrl a` -- and `click`, and `key Tab` -- leave it
# byte-identical, which is a bug report missing the very state it was made to
# show. The suite hit the same thing and answered it the same way.
#
# The example needs a terminal, so it gets a pseudo-terminal from `script` and
# a Ctrl-D to leave by. Skipped with a note where `script` is absent, the way
# the hostile theme and the xcb arm are.
#
# The Ctrl-D is sent SIX TIMES, and that is not superstition. The first
# version of this hung `make check`, which turned out to be a real defect --
# collect_caps() read the terminal's replies and dropped everything else in the
# buffer, so a keystroke arriving before the program had drawn was one nobody
# saw. That is fixed.
#
# The repetition was then tried at ONE, on the theory that the fix made it
# unnecessary, and the theory was wrong: the fix keeps the bytes that arrive
# BEFORE the first ESC, and a byte `script` forwards midway through the reply
# window still falls in the gap the fix deliberately does not close. Sabotaging
# the retention left this arm green, which is what said so -- three passing
# runs at one Ctrl-D were luck, not evidence.
#
# So six, and the comment says which of the two reasons each is for: one lands
# before the window and one after it, and the arm no longer depends on which.
#
# The timeout is the part that is not optional. A gate that can hang is worse
# than no gate: it does not fail, it stops, and `make check` waits for it.
test-tools: all
	@fail=0; \
	out=$$(timeout $(TEST_TIMEOUT) $(INSPECT) < /dev/null 2>/dev/null); \
	case "$$out" in \
	*"widget tree"*aligned*"Enable telemetry"*) echo "    inspect: ok";; \
	*) echo "    inspect: FAILED -- it did not dump a tree"; fail=1;; \
	esac; \
	out=$$(printf 'text hi\nframe\n' \
		| timeout $(TEST_TIMEOUT) $(REPLAY) 2>/dev/null); \
	case "$$out" in \
	*"--- frame 0 ---"*hi*) echo "    replay: ok";; \
	*) echo "    replay: FAILED -- a script produced no frame"; fail=1;; \
	esac; \
	plain=$$(printf 'text hi\nsnapshot\n' \
		| timeout $(TEST_TIMEOUT) $(REPLAY) 2>/dev/null); \
	picked=$$(printf 'text hi\nctrl a\nsnapshot\n' \
		| timeout $(TEST_TIMEOUT) $(REPLAY) 2>/dev/null); \
	flat=$$(printf 'text hi\nframe\n' \
		| timeout $(TEST_TIMEOUT) $(REPLAY) 2>/dev/null); \
	flatpicked=$$(printf 'text hi\nctrl a\nframe\n' \
		| timeout $(TEST_TIMEOUT) $(REPLAY) 2>/dev/null); \
	if [ "$$plain" != "$$picked" ] && [ "$$flat" = "$$flatpicked" ]; then \
		echo "    replay snapshot: ok"; \
	else \
		echo "    replay snapshot: FAILED -- a selection must change the"; \
		echo "                     snapshot and must not change the frame"; \
		fail=1; \
	fi; \
	out=$$(timeout $(TEST_TIMEOUT) $(NEGOTIATE) < /dev/null 2>/dev/null); \
	case "$$out" in \
	*graphics*Halfblocks*"bracketed paste"*no*) echo "    negotiate: ok";; \
	*) echo "    negotiate: FAILED -- a pipe was not reported as a pipe"; fail=1;; \
	esac; \
	out=$$(timeout $(TEST_TIMEOUT) $(NEGOTIATE) --version 2>/dev/null); \
	case "$$out" in \
	*"$(VERSION)"*) echo "    negotiate --version: ok";; \
	*) echo "    negotiate --version: FAILED -- the binary does not say $(VERSION)"; \
	   fail=1;; \
	esac; \
	esc=$$(printf '\033'); \
	out=$$(timeout $(TEST_TIMEOUT) $(NEGOTIATE) --probes < /dev/null 2>/dev/null); \
	case "$$out" in \
	*"$$esc"*) echo "    negotiate --probes: FAILED -- it wrote the query into"; \
	           echo "                        a pipe nothing can answer from"; \
	           fail=1;; \
	*graphics*) echo "    negotiate --probes: ok";; \
	*) echo "    negotiate --probes: FAILED -- no report"; fail=1;; \
	esac; \
	if command -v script >/dev/null 2>&1; then \
		timeout $(TEST_TIMEOUT) script -qec "$(NEGOTIATE) --probes \
			< /dev/null" $(BUILD_DIR)/neg.out > /dev/null 2>&1; \
		n=$$(tr -cd '\033' < $(BUILD_DIR)/neg.out | wc -c); \
		case "$$n:$$(cat $(BUILD_DIR)/neg.out)" in \
		13:*"stdin is not a terminal"*) \
			echo "    negotiate --probes, stdout only: ok";; \
		*) echo "    negotiate --probes, stdout only: FAILED -- $$n escape"; \
		   echo "                        sequence(s) on a terminal nothing"; \
		   echo "                        can answer from"; fail=1;; \
		esac; \
		rm -f $(BUILD_DIR)/neg.out; \
		( i=0; while [ $$i -lt 6 ]; do sleep 0.4; printf '\004'; \
		  i=$$((i + 1)); done ) \
		| timeout $(TEST_TIMEOUT) script -qec "$(EXAMPLE)" \
			$(BUILD_DIR)/chat.out > /dev/null 2>&1; \
		case "$$(tr -d '\000' < $(BUILD_DIR)/chat.out)" in \
		*"did the release build pass"*) echo "    example: ok";; \
		*) echo "    example: FAILED -- it drew no frame"; fail=1;; \
		esac; \
		rm -f $(BUILD_DIR)/chat.out; \
	else \
		echo "    note: script(1) is absent, so the example is not run" >&2; \
	fi; \
	[ "$$fail" -eq 0 ] || { \
		echo "test-tools: a shipped program did not do its job" >&2; exit 1; \
	}; \
	echo "test-tools: 3 tool(s) and the example, 0 failed"

# The suite under valgrind's memcheck, which catches what the sanitizers do not:
# a READ of memory that was never written. That is not a hypothetical gap --
# it found three fixtures in this tree handing QImage(w, h, fmt), whose pixels
# are undefined, to code that encodes them.
#
# The debug build, because valgrind reports addresses without it and a stack
# trace of hexadecimal is a finding nobody can act on. QTTY_UNDER_VALGRIND
# tells the suite to skip the one wall-clock assertion, which under a
# twenty-times-slower instrument measures the instrument.
#
# --error-exitcode makes an error a failure rather than a paragraph scrolling
# past. It is slow -- minutes rather than seconds -- which is why it is its own
# target and not part of `check`.
#
# The log is removed before the run and its absence afterwards is a failure,
# both because of the same incident: a stale log from an earlier run, owned by
# another user, could not be overwritten -- valgrind refused with "Permission
# denied" and the target reported a failure while the four-hour-old log sat
# there reading "0 errors from 0 contexts". An artefact from a previous run is
# not this run's result, and a target that cannot tell them apart invites
# exactly that reading.
test-valgrind:
	@command -v valgrind >/dev/null 2>&1 || { \
		echo "test-valgrind: valgrind is not installed" >&2; exit 1; \
	}
	$(MAKE) tests-build BUILD_DIR=$(DBG_BUILD_DIR) DEBUG=1
	@log="$(DBG_BUILD_DIR)-test/valgrind.log"; \
	rm -f "$$log" || { \
		echo "test-valgrind: cannot replace $$log" >&2; exit 1; \
	}; \
	QTTY_UNDER_VALGRIND=1 QTTY_TEST_TIMEOUT=3000 \
		valgrind --tool=memcheck --track-origins=yes --num-callers=25 \
		--error-exitcode=99 --log-file="$$log" \
		$(DBG_BUILD_DIR)-test/qtty-tests; \
	rc=$$?; \
	if [ ! -s "$$log" ]; then \
		echo "test-valgrind: valgrind wrote no log, so it did not run --" >&2; \
		echo "               this is not a clean result" >&2; exit 1; \
	fi; \
	[ "$$rc" -eq 0 ] || { \
		echo "test-valgrind: exit $$rc; see $$log" >&2; exit 1; \
	}; \
	echo "test-valgrind: clean"

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
# test-tools is in here rather than left as a target somebody remembers,
# because it is seconds and because the whole lesson of this Makefile's other
# arms is that a configuration nobody runs is not a configuration that passed.
# The shipped tools and the example were built by every one of these runs and
# executed by none of them.
# The parts, and then `check` runs them through a sub-make so that it can
# record the verdict either way. A prerequisite list cannot: when a
# prerequisite fails the recipe never runs, so there is nowhere to write down
# that it failed -- and the whole point of the stamp is the FAILING case.
#
# tool/hooks/pre-commit reads it and refuses a commit whose content is known
# to fail. That hook exists because the same mistake happened three times in
# one day: run this target, print its exit status, commit in the same command
# without reading it. Writing the rule down twice did not stop it.
#
# The identity is HEAD plus every uncommitted change to tracked files, which
# is what `git diff HEAD` gives and is unchanged by staging.
CHECK_PARTS = style layout version-check count-check test test-tools test-install
CHECK_STAMP = $(shell git rev-parse --git-common-dir 2>/dev/null)/qtty-check-stamp

check:
	@rm -f "$(CHECK_STAMP)" 2>/dev/null || true
	@id=$$( { git rev-parse HEAD 2>/dev/null; git diff HEAD --binary 2>/dev/null; } \
	        | sha1sum | cut -d' ' -f1 ); \
	echo "RUNNING $$id" > "$(CHECK_STAMP)" 2>/dev/null || true; \
	if $(MAKE) --no-print-directory $(CHECK_PARTS); then \
		echo "PASS $$id" > "$(CHECK_STAMP)" 2>/dev/null || true; \
	else \
		echo "FAIL $$id" > "$(CHECK_STAMP)" 2>/dev/null || true; \
		echo "check: FAILED -- and the stamp says so, so a commit of this" >&2; \
		echo "       content is refused until it is fixed." >&2; \
		exit 1; \
	fi

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

# project.md opens with "N checks, 0 failures", and that number is cited all
# through the document and in nearly every commit message. It is the most
# quoted claim the project makes and nothing was holding it to anything.
#
# This is the shape a peer sweep turned up as the one that goes stale: a claim
# that names something COUNTABLE. Four of those in project.md had outlived
# their subject -- a class said not to exist, a widget said never to be
# exercised -- and each was one grep from being caught, with nothing pointing
# at it. A limit or a judgement cannot go stale the same way; a count can, and
# so a count should carry its proof.
#
# 2>/dev/null is not tidiness, and section 0c says why: the offscreen platform
# writes to stderr, and in a merged stream it lands mid-line and cuts a PASS
# line in half. Two runs of one binary counted 744 and 745 for that reason.
count-check: tests-build
	@stated=$$(sed -n 's/^\([0-9][0-9]*\) checks, 0 failures.*/\1/p' \
		project.md | head -1); \
	actual=$$($(TEST_ENV) timeout $(TEST_TIMEOUT) $(TEST_BIN) 2>/dev/null \
		| grep -c '^PASS:'); \
	if [ -z "$$stated" ]; then \
		echo "count-check: project.md states no check count" >&2; exit 1; \
	fi; \
	if [ "$$stated" != "$$actual" ]; then \
		echo "count-check: project.md says $$stated checks, the suite runs" >&2; \
		echo "             $$actual. One of them is out of date." >&2; \
		exit 1; \
	fi; \
	echo "count-check: project.md and the suite agree at $$actual checks"

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
	install -m 0755 tool/hooks/pre-commit "$$dir/hooks/pre-commit"; \
	echo "hooks: commit-msg and pre-commit installed into $$dir/hooks/"

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

# What this lays down is asserted by `make test-install`, which names every
# file and then checks that `uninstall` takes all of them back.
#
# TWO PROGRAMS ARE NOT HERE, and this comment is the flag rather than the fix.
# `qtty-negotiate` is built, shipped in the source tree, and documented in
# doc/beerssh.md as a thing to run against a terminal -- and it is not
# installed. The chat example is not either, which is ordinary for an example.
# The help line above said "tools and the example" until 2026-09-03 and was
# wrong about both. Whether the negotiator belongs in $(PREFIX)/bin is the
# copyright holder's call, and project.md section 8.0 carries it.
# The pkg-config file is GENERATED rather than tracked, because everything in
# it is already stated somewhere else and a second copy is a second thing to
# be wrong: the version comes from VERSION, the prefix from PREFIX, and the
# flags from what the library actually needs. Written into BUILD_DIR so a
# `make clean` takes it and the source tree stays free of build output.
#
# Cflags carries -std=c++17 because qtty.pri is not installed and a consumer
# has nowhere else to learn the standard. Libs.private is empty on purpose:
# the static library needs nothing beyond Qt, `openpty` being a TEST
# dependency only -- grep says so, and test/test.pro is where -lutil is
# added.
# FORCE, not just VERSION and Makefile: PREFIX is a variable rather than a
# file, so a build that made this once for one prefix would hand the next
# install a file naming the old one. Measured -- staged with PREFIX=/usr by
# `test-install`, then installed elsewhere, and the installed .pc still said
# prefix=/usr, so pkg-config dropped the include path as a system one and a
# consumer got a compile line with no qtty headers in it. Regenerating costs
# a printf.
$(BUILD_DIR)/qtty.pc: VERSION Makefile FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' \
	    'prefix=$(PREFIX)' \
	    'exec_prefix=$${prefix}' \
	    'libdir=$${prefix}/lib' \
	    'includedir=$${prefix}/include' \
	    '' \
	    'Name: qtty' \
	    'Description: Qt Widgets rendered into a terminal' \
	    'Version: $(VERSION)' \
	    'Requires: Qt6Widgets' \
	    'Cflags: -I$${includedir} -std=c++17' \
	    'Libs: -L$${libdir} -lqtty' \
	    > $@

install: $(LIB) $(BUILD_DIR)/qtty.pc
	install -d $(DESTDIR)$(PREFIX)/include/qtty
	install -m 0644 include/qtty/*.h $(DESTDIR)$(PREFIX)/include/qtty/
	install -d $(DESTDIR)$(PREFIX)/lib
	install -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 0644 $(BUILD_DIR)/qtty.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(INSPECT) $(REPLAY) $(DESTDIR)$(PREFIX)/bin/

# Named, not globbed: a clean or an uninstall is the one target everybody runs
# without reading it.
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/qtty-inspect
	rm -f $(DESTDIR)$(PREFIX)/bin/qtty-replay
	rm -f $(DESTDIR)$(PREFIX)/lib/libqtty.a
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/qtty.pc
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

.PHONY: all test test-platforms test-sanitize test-valgrind test-tools test-install count-check tests-build coverage record check style style-source style-docs layout hooks \
        version-check run install uninstall clean veryclean distclean help \
        test-screen test-negotiate test-consume sabotage FORCE
