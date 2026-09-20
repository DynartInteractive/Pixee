QT       += core gui widgets sql testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_FolderSuggester

INCLUDEPATH += $$PWD/.. $$PWD/../../src

# filterAndSort is pure, but it orders through FileModel::nameLessThan so the
# popup matches the browser's ordering — which drags FileModel's own
# dependencies in at link time even though none of them are exercised here.
SOURCES += \
    tst_FolderSuggester.cpp \
    $$PWD/../../src/FolderSuggester.cpp \
    $$PWD/../../src/FileModel.cpp \
    $$PWD/../../src/FileItem.cpp \
    $$PWD/../../src/FolderEnumerator.cpp \
    $$PWD/../../src/FolderRefresher.cpp \
    $$PWD/../../src/Config.cpp \
    $$PWD/../../src/ImageFormats.cpp \
    $$PWD/../../src/Theme.cpp \
    $$PWD/../../src/IcoUtils.cpp \
    $$PWD/../../src/ThumbnailCache.cpp \
    $$PWD/../../src/ThumbnailDatabase.cpp \
    $$PWD/../../src/ThumbnailGenerator.cpp \
    $$PWD/../../src/ThumbnailWorker.cpp

HEADERS += \
    $$PWD/../../src/FolderSuggester.h \
    $$PWD/../../src/FileModel.h \
    $$PWD/../../src/FileItem.h \
    $$PWD/../../src/FolderEnumerator.h \
    $$PWD/../../src/FolderRefresher.h \
    $$PWD/../../src/Config.h \
    $$PWD/../../src/ImageFormats.h \
    $$PWD/../../src/Theme.h \
    $$PWD/../../src/IcoUtils.h \
    $$PWD/../../src/ThumbnailCache.h \
    $$PWD/../../src/ThumbnailDatabase.h \
    $$PWD/../../src/ThumbnailGenerator.h \
    $$PWD/../../src/ThumbnailWorker.h
