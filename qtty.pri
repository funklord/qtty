# Settings shared by every qtty .pro file. Included rather than repeated so
# that the C++ standard, the warning set, the optimisation level and -- above
# all -- where libqtty.a lands are stated once.

QTTY_ROOT = $$PWD

# Where the static library is written and looked for. $$shadowed() maps a
# source directory to its build directory, so this is $$PWD/lib for an
# in-place build and BUILD_DIR/lib for a shadow build. It used to be
# $$PWD/../lib unconditionally, which wrote the library into the source tree
# whatever qmake was told -- lib/libqtty.a was a tracked file that every
# build rewrote, and a shadow build still dirtied the repository.
# A build driven from elsewhere -- the test build -- passes this in, because
# $$shadowed() can only answer for the tree qmake was pointed at.
isEmpty(QTTY_LIB_DIR): QTTY_LIB_DIR = $$shadowed($$PWD)/lib
QTTY_LIB = $$QTTY_LIB_DIR/libqtty.a

CONFIG += c++17
INCLUDEPATH += $$QTTY_ROOT/include

QMAKE_CXXFLAGS += -Wall -Wextra

# -Os, not qmake's -O2 release default. Replaced rather than appended: two -O
# flags on one command line leave the last one winning, which makes the
# setting depend on where in the line qmake happened to put it.
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -Os
QMAKE_CFLAGS_RELEASE   -= -O2
QMAKE_CFLAGS_RELEASE   += -Os

# -Og rather than qmake's -O0 for a debug build, for the same reason: it stays
# followable in a debugger without giving up everything.
QMAKE_CXXFLAGS_DEBUG -= -O0
QMAKE_CXXFLAGS_DEBUG += -Og

# AND LET THE LINKER DROP WHAT NOTHING CALLS. Set by the copyright holder
# 2026-11-05 beside -Os, and said explicitly here for the same reason -Os is
# rather than left to a qmake CONFIG switch: without the flags the linker
# keeps every object on the link line WHOLE, so each unused function in this
# library rides along in every binary that links one of its objects -- which
# -Os cannot see, working one function at a time.
#
# In both flavours and not only the release one: the debug build links the
# same archive, and a figure measured on one configuration is a figure about
# that configuration.
#
# WHAT HAD TO BE CHECKED HERE, because both ways of getting this wrong fail
# quietly. Code reached only BY NAME is not "called", and this library is
# full of exactly that: a QStyle is reached through Qt's metaobject system,
# a QPaintEngine through virtuals, and the backends through an interface. So
# the suite was run against the COLLECTED binaries rather than the flags
# being trusted -- see project.md 8.696 for the figures and the run.
QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
QMAKE_CFLAGS   += -ffunction-sections -fdata-sections
QMAKE_LFLAGS   += -Wl,--gc-sections

# The version is stated in one place, the VERSION file, and read from it.
# Stating it here as well is how the two drift; `make version-check` asserts
# this line still reads the file.
VERSION = $$cat($$PWD/VERSION, singleline)
DEFINES += QTTY_VERSION_STRING=\\\"$$VERSION\\\"
