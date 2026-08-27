# qtty-tests -- one binary aggregating every suite_*.cpp. `make test` at the
# top level builds and runs it; CONFIG testcase also wires it to qmake's own
# `make check`.
include(../qtty.pri)

TEMPLATE = app
TARGET = qtty-tests
CONFIG += console testcase
CONFIG -= app_bundle
QT += widgets
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB

# The fixtures under test/snapshot/ are read from the source tree, which the
# binary cannot find on its own once the build moved out of it.
DEFINES += QTTY_SOURCE_DIR=\\\"$$QTTY_ROOT\\\"

# NullBackend is the section 9 harness backend and lives in src/ rather than
# in the public headers, the same way tool/replay reaches AnsiBackend.
INCLUDEPATH += $$QTTY_ROOT

SOURCES += main.cpp \
           suite_cells.cpp \
           suite_theme.cpp \
           suite_render.cpp \
           suite_grid.cpp \
           suite_placements.cpp \
           suite_router.cpp \
           suite_widgets.cpp \
           suite_graphics.cpp \
           suite_runtime.cpp \
           suite_backend.cpp \
           suite_budget.cpp
