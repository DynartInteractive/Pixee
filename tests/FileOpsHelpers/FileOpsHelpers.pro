QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_FileOpsHelpers

# `..` for the shared test fixtures (TestHelpers.h);
# `../../src` for the app source tree (FileOpsHelpers.h).
INCLUDEPATH += $$PWD/.. $$PWD/../../src

SOURCES += \
    tst_FileOpsHelpers.cpp \
    $$PWD/../TestHelpers.cpp \
    $$PWD/../../src/FileOpsHelpers.cpp

HEADERS += \
    $$PWD/../TestHelpers.h \
    $$PWD/../../src/FileOpsHelpers.h
