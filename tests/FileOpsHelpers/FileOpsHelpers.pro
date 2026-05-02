QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_FileOpsHelpers

# `..` for the shared test fixtures (TestHelpers.h);
# `../..` for the app source tree (FileOpsHelpers.h).
INCLUDEPATH += $$PWD/.. $$PWD/../..

SOURCES += \
    tst_FileOpsHelpers.cpp \
    $$PWD/../TestHelpers.cpp \
    $$PWD/../../FileOpsHelpers.cpp

HEADERS += \
    $$PWD/../TestHelpers.h \
    $$PWD/../../FileOpsHelpers.h
