# qtty -- top-level project file. The entry point is the hand-written
# Makefile beside it, not this: `make` drives qmake into BUILD_DIR, so a
# build never writes into the source tree. `qmake6 ../qtty.pro && make` in a
# directory of your own works too, and is what the Makefile does.
TEMPLATE = subdirs
SUBDIRS = src test tool_inspect tool_replay example_chat
tool_inspect.subdir  = tool/inspect
tool_replay.subdir   = tool/replay
example_chat.subdir  = example/chat
test.depends         = src
tool_inspect.depends = src
tool_replay.depends  = src
example_chat.depends = src
