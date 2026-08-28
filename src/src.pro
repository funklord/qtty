# libqtty -- the static library. Everything else in the tree links this.
include(../qtty.pri)

TEMPLATE = lib
TARGET = qtty
CONFIG += staticlib
QT += widgets
DESTDIR = $$QTTY_LIB_DIR

HEADERS += \
    ../include/qtty/qtty.h \
    ../include/qtty/version.h \
    ../include/qtty/color.h \
    ../include/qtty/cell.h \
    ../include/qtty/theme.h \
    ../include/qtty/backend.h \
    ../include/qtty/grid.h \
    ../include/qtty/paint.h \
    ../include/qtty/delegate.h \
    ../include/qtty/runtime.h \
    ../include/qtty/application.h \
    ../include/qtty/testing.h \
    ../include/qtty/graphics.h \
    ../include/qtty/overlay.h \
    backend/ansi/ansi_backend.h \
    backend/ansi/term_caps.h \
    backend/ansi/scroll_settle.h \
    backend/null/null_backend.h

SOURCES += \
    core/cell_buffer.cpp \
    core/color.cpp \
    core/theme.cpp \
    grid/grid_style.cpp \
    render/cell_paint.cpp \
    widget/cell_item_delegate.cpp \
    runtime/input_router.cpp \
    runtime/compositor.cpp \
    runtime/application.cpp \
    graphics/graphics.cpp \
    graphics/overlay.cpp \
    backend/ansi/ansi_backend.cpp \
    backend/ansi/term_caps.cpp
