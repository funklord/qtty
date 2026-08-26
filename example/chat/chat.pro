# example/chat -- dual-frontend chat, the canonical qtty example.
# The dual binary is the one to ship; compile-time variants are policy builds:
#   qmake "DEFINES+=QTTY_NO_TUI"   -> GUI-only
#   qmake "DEFINES+=QTTY_NO_GUI"   -> TUI-only
# Packaging: install `chat`, optionally a `chat-tui` symlink (argv[0] selects).
include(../../qtty.pri)

TEMPLATE = app
TARGET = chat
CONFIG += console
CONFIG -= app_bundle
QT += widgets
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB
HEADERS += chat.h
SOURCES += main.cpp
