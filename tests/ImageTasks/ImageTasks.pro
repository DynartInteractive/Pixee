QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_ImageTasks

INCLUDEPATH += $$PWD/.. $$PWD/../..

SOURCES += \
    tst_ImageTasks.cpp \
    $$PWD/../TestHelpers.cpp \
    $$PWD/../TaskTestFixture.cpp \
    $$PWD/../../FileOpsHelpers.cpp \
    $$PWD/../../Task.cpp \
    $$PWD/../../TaskGroup.cpp \
    $$PWD/../../TaskRunner.cpp \
    $$PWD/../../TaskManager.cpp \
    $$PWD/../../ScaleImageTask.cpp \
    $$PWD/../../ConvertFormatTask.cpp

HEADERS += \
    $$PWD/../TestHelpers.h \
    $$PWD/../TaskTestFixture.h \
    $$PWD/../../FileOpsHelpers.h \
    $$PWD/../../Task.h \
    $$PWD/../../TaskGroup.h \
    $$PWD/../../TaskRunner.h \
    $$PWD/../../TaskManager.h \
    $$PWD/../../ScaleImageTask.h \
    $$PWD/../../ConvertFormatTask.h
