# qtty -- top-level project file. test/ is deliberately NOT a SUBDIR here:
# the default build produces the library, the tools and the example, and
# `make test` drives test/test.pro into its own build directory. See the
# Makefile's TEST section for why that is not merely tidiness.
#
# The entry point is the hand-written Makefile beside this file, not this
# file: `make` drives qmake into BUILD_DIR, so a build never writes into the
# source tree. `qmake6 ../qtty.pro && make` in a directory of your own works
# too, and is what the Makefile does.
TEMPLATE = subdirs
SUBDIRS = src tool_inspect tool_replay example_chat
tool_inspect.subdir  = tool/inspect
tool_replay.subdir   = tool/replay
example_chat.subdir  = example/chat
tool_inspect.depends = src
tool_replay.depends  = src
example_chat.depends = src
