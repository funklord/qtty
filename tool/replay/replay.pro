include(../../qtty.pri)

TEMPLATE = app
TARGET = qtty-replay
CONFIG += console
CONFIG -= app_bundle
QT += widgets
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB
# --ansi drives the real AnsiBackend, which lives inside the library rather
# than in a public header, so the source root is on the include path too.
INCLUDEPATH += $$QTTY_ROOT
SOURCES += main.cpp
