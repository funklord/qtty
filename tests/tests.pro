# qtty test runner — `make check` runs it (CONFIG testcase)
TEMPLATE = app
TARGET = qtty-tests
CONFIG += c++17 console testcase
CONFIG -= app_bundle
QT += widgets
INCLUDEPATH += $$PWD/../include
LIBS += -L$$PWD/../lib -lqtty
PRE_TARGETDEPS += $$PWD/../lib/libqtty.a
DEFINES += QTTY_SOURCE_DIR=\\\"$$PWD/..\\\"
SOURCES += main.cpp suite_cells.cpp suite_theme.cpp suite_render.cpp \
           suite_grid.cpp suite_placements.cpp suite_router.cpp suite_widgets.cpp
