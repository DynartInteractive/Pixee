QT       += core gui sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# Single source of truth for the app version: the repo-root VERSION.txt file.
# (The .txt extension matters — an extensionless "VERSION" at the repo root,
# which is on the compiler include path, shadows the C++ <version> header on
# case-insensitive Windows and breaks the build.) qmake's VERSION also stamps
# the Windows .exe file-properties resource (FILEVERSION/PRODUCTVERSION);
# APP_VERSION exposes the same string to the code (About dialog, --version).
# The installer reads the same file — keep this the only place the number lives.
VERSION = $$cat($$PWD/VERSION.txt)
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

# Optional Exiv2 metadata backend for the Info panel. OFF by default: the app
# builds and shows Qt-only basics (dimensions/format/size) without it. To turn
# it on, drop the prebuilt Exiv2 MSVC binaries into thirdparty/exiv2/ (see
# docs/metadata.md for the exact files) and build with PIXEE_HAVE_EXIV2=1:
#     qmake PIXEE_HAVE_EXIV2=1 Pixee.pro
# The MetadataReader's EXIF/IPTC/XMP parse is guarded by the same define, so it
# only compiles when the headers are actually present. Exiv2 is GPLv2+ — see
# the licence note in docs/metadata.md before shipping.
!isEmpty(PIXEE_HAVE_EXIV2) {
    DEFINES     += PIXEE_HAVE_EXIV2
    INCLUDEPATH += $$PWD/thirdparty/exiv2/include
    LIBS        += -L$$PWD/thirdparty/exiv2/lib -lexiv2
    message("Exiv2 metadata backend: ENABLED")
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/Config.cpp \
    src/ConflictDialog.cpp \
    src/ConvertFormatTask.cpp \
    src/CopyFileTask.cpp \
    src/DeleteFileTask.cpp \
    src/FileFilterModel.cpp \
    src/FileItem.cpp \
    src/FileListView.cpp \
    src/FileListViewDelegate.cpp \
    src/FileModel.cpp \
    src/FileOpsHelpers.cpp \
    src/FileOpsMenuBuilder.cpp \
    src/FolderCleanupTask.cpp \
    src/FolderEnumerator.cpp \
    src/FolderRefresher.cpp \
    src/FolderTreeView.cpp \
    src/IcoUtils.cpp \
    src/ImageLoader.cpp \
    src/MainWindow.cpp \
    src/BatchRenameDialog.cpp \
    src/BatchRenamePlan.cpp \
    src/SaveAsDialog.cpp \
    src/SaveImageTask.cpp \
    src/MetadataPanel.cpp \
    src/MetadataReader.cpp \
    src/MoveFileTask.cpp \
    src/RenameTask.cpp \
    src/NewFolderDialog.cpp \
    src/OpenWithDialog.cpp \
    src/Pixee.cpp \
    src/RenameDialog.cpp \
    src/ScaleImageTask.cpp \
    src/SettingsDialog.cpp \
    src/Task.cpp \
    src/TaskConflictPrompter.cpp \
    src/TaskDockWidget.cpp \
    src/TaskGroup.cpp \
    src/TaskGroupWidget.cpp \
    src/TaskItemWidget.cpp \
    src/TaskManager.cpp \
    src/TaskRunner.cpp \
    src/TaskStatusWidget.cpp \
    src/Toast.cpp \
    src/Theme.cpp \
    src/ThumbnailCache.cpp \
    src/ThumbnailDatabase.cpp \
    src/ThumbnailGenerator.cpp \
    src/ThumbnailWorker.cpp \
    src/ViewerWidget.cpp \
    src/main.cpp

HEADERS += \
    src/AppSettings.h \
    src/Config.h \
    src/ConflictDialog.h \
    src/ConvertFormatTask.h \
    src/CopyFileTask.h \
    src/DeleteFileTask.h \
    src/FileFilterModel.h \
    src/FileItem.h \
    src/FileListView.h \
    src/FileListViewDelegate.h \
    src/FileModel.h \
    src/FileOpsHelpers.h \
    src/FileOpsMenuBuilder.h \
    src/FileType.h \
    src/FolderCleanupTask.h \
    src/FolderEnumerator.h \
    src/FolderRefresher.h \
    src/FolderTreeView.h \
    src/IcoUtils.h \
    src/BatchRenameDialog.h \
    src/BatchRenamePlan.h \
    src/SaveAsDialog.h \
    src/SaveImageTask.h \
    src/ImageLoader.h \
    src/ImageMetadata.h \
    src/MainWindow.h \
    src/MetadataPanel.h \
    src/MetadataReader.h \
    src/MoveFileTask.h \
    src/RenameTask.h \
    src/NewFolderDialog.h \
    src/OpenWithDialog.h \
    src/Pixee.h \
    src/RenameDialog.h \
    src/ScaleImageTask.h \
    src/SettingsDialog.h \
    src/Task.h \
    src/TaskConflictPrompter.h \
    src/TaskDockWidget.h \
    src/TaskGroup.h \
    src/TaskGroupWidget.h \
    src/TaskItemWidget.h \
    src/TaskManager.h \
    src/TaskRunner.h \
    src/TaskStatusWidget.h \
    src/Toast.h \
    src/Theme.h \
    src/ThumbnailCache.h \
    src/ThumbnailDatabase.h \
    src/ThumbnailGenerator.h \
    src/ThumbnailWorker.h \
    src/ViewerWidget.h

TRANSLATIONS += \
    translations/Pixee_en_US.ts \
    translations/Pixee_hu.ts \
    translations/Pixee_de.ts \
    translations/Pixee_fr.ts \
    translations/Pixee_es.ts

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

!equals(PWD, $$TARGET_DIR) {
    copy_themes.commands = $(COPY_DIR) $$shell_path($$PWD/themes) $$shell_path($$TARGET_DIR/themes)
} else {
    copy_themes.commands = @echo \"In-source build — themes already in place, skipping copy.\"
}
first.depends = $(first) copy_themes
export(first.depends)
export(copy_themes.commands)
QMAKE_EXTRA_TARGETS += first copy_themes

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
