include(../../qtty.pri)

TEMPLATE = app
TARGET = qtty-inspect
CONFIG += console
CONFIG -= app_bundle
QT += widgets
LIBS += -L$$QTTY_LIB_DIR -lqtty
PRE_TARGETDEPS += $$QTTY_LIB
SOURCES += main.cpp
