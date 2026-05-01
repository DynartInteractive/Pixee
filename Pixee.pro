QT       += core gui sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    Config.cpp \
    ConvertFormatTask.cpp \
    CopyFileTask.cpp \
    DeleteFileTask.cpp \
    FileFilterModel.cpp \
    FileItem.cpp \
    FileListView.cpp \
    FileListViewDelegate.cpp \
    FileModel.cpp \
    FileOpsMenuBuilder.cpp \
    FolderEnumerator.cpp \
    FolderTreeView.cpp \
    ImageLoader.cpp \
    MainWindow.cpp \
    MoveFileTask.cpp \
    Pixee.cpp \
    ScaleImageTask.cpp \
    Task.cpp \
    TaskDockWidget.cpp \
    TaskGroup.cpp \
    TaskGroupWidget.cpp \
    TaskItemWidget.cpp \
    TaskManager.cpp \
    TaskRunner.cpp \
    Toast.cpp \
    Theme.cpp \
    ThumbnailCache.cpp \
    ThumbnailDatabase.cpp \
    ThumbnailGenerator.cpp \
    ThumbnailWorker.cpp \
    ViewerWidget.cpp \
    main.cpp

HEADERS += \
    Config.h \
    ConvertFormatTask.h \
    CopyFileTask.h \
    DeleteFileTask.h \
    FileFilterModel.h \
    FileItem.h \
    FileListView.h \
    FileListViewDelegate.h \
    FileModel.h \
    FileOpsMenuBuilder.h \
    FileType.h \
    FolderEnumerator.h \
    FolderTreeView.h \
    ImageLoader.h \
    MainWindow.h \
    MoveFileTask.h \
    Pixee.h \
    ScaleImageTask.h \
    Task.h \
    TaskDockWidget.h \
    TaskGroup.h \
    TaskGroupWidget.h \
    TaskItemWidget.h \
    TaskManager.h \
    TaskRunner.h \
    Toast.h \
    Theme.h \
    ThumbnailCache.h \
    ThumbnailDatabase.h \
    ThumbnailGenerator.h \
    ThumbnailWorker.h \
    ViewerWidget.h

TRANSLATIONS += \
    Pixee_en_US.ts

CONFIG += lrelease
CONFIG += embed_translations

RESOURCES += \
    resources.qrc

# Copy themes/ next to the built executable on every build.
# Resolve the actual binary directory: explicit DESTDIR wins; otherwise
# debug_and_release builds land in debug/ or release/ subdirs, while
# single-config builds land flat in OUT_PWD.
isEmpty(DESTDIR) {
    debug_and_release {
        CONFIG(debug, debug|release): TARGET_DIR = $$OUT_PWD/debug
        else: TARGET_DIR = $$OUT_PWD/release
    } else {
        TARGET_DIR = $$OUT_PWD
    }
} else {
    TARGET_DIR = $$DESTDIR
}

copy_themes.commands = $(COPY_DIR) $$shell_path($$PWD/themes) $$shell_path($$TARGET_DIR/themes)
first.depends = $(first) copy_themes
export(first.depends)
export(copy_themes.commands)
QMAKE_EXTRA_TARGETS += first copy_themes

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
