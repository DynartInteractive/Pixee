QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_DragPaths

INCLUDEPATH += $$PWD/.. $$PWD/../../src

SOURCES += \
    tst_DragPaths.cpp \
    $$PWD/../../src/DragPaths.cpp \
    $$PWD/../../src/FileOpsHelpers.cpp

HEADERS += \
    $$PWD/../../src/DragPaths.h \
    $$PWD/../../src/FileOpsHelpers.h
