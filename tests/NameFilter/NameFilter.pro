QT       += core testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_NameFilter

INCLUDEPATH += $$PWD/.. $$PWD/../../src

# Pure string matching - no GUI, no model, no filesystem.
SOURCES += \
    tst_NameFilter.cpp \
    $$PWD/../../src/NameFilter.cpp

HEADERS += \
    $$PWD/../../src/NameFilter.h
