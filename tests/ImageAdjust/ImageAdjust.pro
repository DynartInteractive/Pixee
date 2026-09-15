QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_ImageAdjust

INCLUDEPATH += $$PWD/.. $$PWD/../../src

# Pure colour-adjustment maths - no GUI, no filesystem, no task layer.
SOURCES += \
    tst_ImageAdjust.cpp \
    $$PWD/../../src/ImageAdjust.cpp

HEADERS += \
    $$PWD/../../src/ImageAdjust.h
