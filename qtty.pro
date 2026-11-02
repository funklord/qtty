# qtty -- top-level project file. test/ is deliberately NOT a SUBDIR here:
# this project makes the library, the four tools and the example, and
# `make test` drives test/test.pro into its own build directory. See the
# Makefile's TEST section for why that is not merely tidiness.
#
# A BARE `make` DOES NOT BUILD ALL OF THESE. The Makefile beside this file
# asks for `sub-src` alone, because a default builds the software and the
# tools and the example are helpers around it -- see its $(LIB) rule, and
# project.md 0f. Running `make` inside a build directory of your own, with
# no target, does build all six: that is this file's own default and it is
# the one place the distinction shows.
#
# The entry point is the hand-written Makefile beside this file, not this
# file: `make` drives qmake into BUILD_DIR, so a build never writes into the
# source tree. `qmake6 ../qtty.pro && make` in a directory of your own works
# too, and is what the Makefile does.
TEMPLATE = subdirs
SUBDIRS = src tool_inspect tool_replay tool_negotiate tool_tray example_chat
tool_inspect.subdir  = tool/inspect
tool_replay.subdir   = tool/replay
tool_negotiate.subdir = tool/negotiate
tool_tray.subdir     = tool/tray
example_chat.subdir  = example/chat
tool_inspect.depends = src
tool_replay.depends  = src
tool_negotiate.depends = src
tool_tray.depends    = src
example_chat.depends = src
