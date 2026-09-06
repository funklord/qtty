include(../../qtty.pri)

TEMPLATE = app
TARGET = qtty-tray-check
CONFIG += console
CONFIG -= app_bundle
QT += widgets dbus
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB

SOURCES += main.cpp
