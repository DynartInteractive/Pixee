QT       += core testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_BatchRename

INCLUDEPATH += $$PWD/.. $$PWD/../../src

# Pure name-computation + rename-ordering logic — no GUI, no filesystem.
SOURCES += \
    tst_BatchRename.cpp \
    $$PWD/../../src/BatchRenamePlan.cpp

HEADERS += \
    $$PWD/../../src/BatchRenamePlan.h
