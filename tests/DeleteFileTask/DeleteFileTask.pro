QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_DeleteFileTask

INCLUDEPATH += $$PWD/.. $$PWD/../../src

SOURCES += \
    tst_DeleteFileTask.cpp \
    $$PWD/../TestHelpers.cpp \
    $$PWD/../TaskTestFixture.cpp \
    $$PWD/../../src/FileOpsHelpers.cpp \
    $$PWD/../../src/Task.cpp \
    $$PWD/../../src/TaskGroup.cpp \
    $$PWD/../../src/TaskRunner.cpp \
    $$PWD/../../src/TaskManager.cpp \
    $$PWD/../../src/DeleteFileTask.cpp

HEADERS += \
    $$PWD/../TestHelpers.h \
    $$PWD/../TaskTestFixture.h \
    $$PWD/../../src/FileOpsHelpers.h \
    $$PWD/../../src/Task.h \
    $$PWD/../../src/TaskGroup.h \
    $$PWD/../../src/TaskRunner.h \
    $$PWD/../../src/TaskManager.h \
    $$PWD/../../src/DeleteFileTask.h
