# qtty — top-level project. Build: qmake && make       (gmake/fmake both fine)
# Tests:                          make check
TEMPLATE = subdirs
SUBDIRS = src tests tools_inspect tools_replay example_chat
tests.subdir          = tests
tools_inspect.subdir  = tools/inspect
tools_replay.subdir   = tools/replay
example_chat.subdir   = examples/chat
tests.depends = src
tools_inspect.depends = src
tools_replay.depends  = src
example_chat.depends  = src
