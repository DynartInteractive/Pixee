QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_Histogram

INCLUDEPATH += $$PWD/.. $$PWD/../../src

# Pure binning maths - no GUI, no filesystem.
SOURCES += \
    tst_Histogram.cpp \
    $$PWD/../../src/Histogram.cpp

HEADERS += \
    $$PWD/../../src/Histogram.h
