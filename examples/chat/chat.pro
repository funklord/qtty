# examples/chat — dual-frontend chat, the canonical qtty example.
# The dual binary is the one to ship; compile-time variants are policy builds:
#   qmake "DEFINES+=QTTY_NO_TUI"   -> GUI-only
#   qmake "DEFINES+=QTTY_NO_GUI"   -> TUI-only
# Packaging: install `chat`, optionally a `chat-tui` symlink (argv[0] selects).
TEMPLATE = app
TARGET = chat
CONFIG += c++17 console
CONFIG -= app_bundle
QT += widgets
INCLUDEPATH += $$PWD/../../include
LIBS += -L$$PWD/../../lib -lqtty
PRE_TARGETDEPS += $$PWD/../../lib/libqtty.a
HEADERS += chat.h
SOURCES += main.cpp
