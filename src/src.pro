# libqtty — static library
TEMPLATE = lib
TARGET = qtty
CONFIG += staticlib c++17
QT += widgets
DESTDIR = $$PWD/../lib
INCLUDEPATH += $$PWD/../include

HEADERS += \
    ../include/qtty/qtty.h \
    ../include/qtty/version.h \
    ../include/qtty/color.h \
    ../include/qtty/cell.h \
    ../include/qtty/theme.h \
    ../include/qtty/backend.h \
    ../include/qtty/grid.h \
    ../include/qtty/paint.h \
    ../include/qtty/runtime.h \
    ../include/qtty/application.h \
    ../include/qtty/testing.h \
    backends/ansi/ansibackend.h \
    backends/null/nullbackend.h

SOURCES += \
    core/cellbuffer.cpp \
    core/color.cpp \
    core/theme.cpp \
    grid/gridstyle.cpp \
    render/cellpaint.cpp \
    runtime/inputrouter.cpp \
    runtime/compositor.cpp \
    runtime/application.cpp \
    backends/ansi/ansibackend.cpp
