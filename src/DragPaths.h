#ifndef DRAGPATHS_H
#define DRAGPATHS_H

#include <QMimeData>
#include <QString>
#include <QStringList>

// The payload of a drag that starts in Pixee, and how a drop reads one.
//
// Why this exists: QMimeData::setUrls is the obvious way to drag files, but
// on Windows Qt exports a text/uri-list drag as CF_HDROP *and* as a URL
// (the UniformResourceLocator formats). A Chromium-based receiver — Discord,
// Slack, a browser upload box — then sees "text/uri-list" and "text/plain"
// beside "Files" and treats the drag as a link, not as files, and refuses
// it; Explorer and Total Commander offer CF_HDROP alone and are accepted.
// Verified with a local page: a Pixee drag arrived as
// [text/plain, text/uri-list, Files], an Explorer drag as [Files].
//
// So on Windows a Pixee drag carries its paths in a private MIME type that
// a QWindowsMimeConverter registered at startup turns into CF_HDROP and
// nothing else. Inside the process Qt hands the drop the original QMimeData,
// so localPaths() reads the private type first; a drop from Explorer or
// another Pixee window arrives as CF_HDROP, which Qt presents as URLs, and
// localPaths() falls back to those. Other platforms use setUrls as before.
namespace DragPaths {

// The private type: UTF-8 paths, one per line, forward slashes.
extern const char* const kMime;

// Mime data for a QDrag over `paths`. Also carries the "Preferred
// DropEffect" = COPY that Explorer keys off. Caller owns it (QDrag takes it).
// Returns nullptr for an empty list.
QMimeData* makeDragMimeData(const QStringList& paths);

// Local file-system paths found in drag/clipboard data: the private type if
// present, otherwise every local-file URL. Empty when there is none.
QStringList localPaths(const QMimeData* mime);
bool hasLocalPaths(const QMimeData* mime);

// Windows: register the CF_HDROP converter. No-op elsewhere. Call once,
// after the QApplication exists (the converter registry lives in it).
void installNativeConverter();

}  // namespace DragPaths

#endif  // DRAGPATHS_H
