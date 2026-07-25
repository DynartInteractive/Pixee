QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_ImageTasks

INCLUDEPATH += $$PWD/.. $$PWD/../../src

SOURCES += \
    tst_ImageTasks.cpp \
    $$PWD/../TestHelpers.cpp \
    $$PWD/../TaskTestFixture.cpp \
    $$PWD/../../src/FileOpsHelpers.cpp \
    $$PWD/../../src/ImageFormats.cpp \
    $$PWD/../../src/Task.cpp \
    $$PWD/../../src/TaskGroup.cpp \
    $$PWD/../../src/TaskRunner.cpp \
    $$PWD/../../src/TaskManager.cpp \
    $$PWD/../../src/ScaleImageTask.cpp \
    $$PWD/../../src/ConvertFormatTask.cpp \
    $$PWD/../../src/SaveImageTask.cpp

HEADERS += \
    $$PWD/../TestHelpers.h \
    $$PWD/../TaskTestFixture.h \
    $$PWD/../../src/FileOpsHelpers.h \
    $$PWD/../../src/ImageFormats.h \
    $$PWD/../../src/Task.h \
    $$PWD/../../src/TaskGroup.h \
    $$PWD/../../src/TaskRunner.h \
    $$PWD/../../src/TaskManager.h \
    $$PWD/../../src/ScaleImageTask.h \
    $$PWD/../../src/ConvertFormatTask.h \
    $$PWD/../../src/SaveImageTask.h
