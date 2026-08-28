include(../../qtty.pri)

TEMPLATE = app
TARGET = qtty-negotiate
CONFIG += console
CONFIG -= app_bundle
QT += widgets
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB

# Reaches into src/ for AnsiBackend the same way tool/replay does: the backend
# is not public API, and this tool exists to report on it.
INCLUDEPATH += $$QTTY_ROOT
SOURCES += main.cpp
