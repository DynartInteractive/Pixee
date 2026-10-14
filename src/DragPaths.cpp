#include "DragPaths.h"

#include <QDir>
#include <QList>
#include <QUrl>

#include "FileOpsHelpers.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <QtGui/qwindowsmimeconverter.h>
#endif

namespace DragPaths {

const char* const kMime = "application/x-pixee-paths";

QMimeData* makeDragMimeData(const QStringList& paths) {
    if (paths.isEmpty()) return nullptr;
    auto* mime = new QMimeData();
#ifdef Q_OS_WIN
    mime->setData(kMime, paths.join(QLatin1Char('\n')).toUtf8());
#else
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& p : paths) urls.append(QUrl::fromLocalFile(p));
    mime->setUrls(urls);
#endif
    mime->setData(FileOpsHelpers::kDropEffectMime, FileOpsHelpers::dropEffectBytes(5));
    return mime;
}

QStringList localPaths(const QMimeData* mime) {
    QStringList out;
    if (!mime) return out;
    if (mime->hasFormat(QLatin1String(kMime))) {
        const QStringList lines =
            QString::fromUtf8(mime->data(QLatin1String(kMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& l : lines) out.append(l);
        return out;
    }
    if (!mime->hasUrls()) return out;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) out.append(url.toLocalFile());
    }
    return out;
}

bool hasLocalPaths(const QMimeData* mime) {
    return !localPaths(mime).isEmpty();
}

#ifdef Q_OS_WIN
namespace {

// Publishes kMime as CF_HDROP — the one format Explorer offers for files —
// and nothing more. Qt's own text/uri-list converter never sees a Pixee
// drag because the drag carries no URLs. The to-mime direction is left to
// Qt: an incoming CF_HDROP becomes text/uri-list as it always has.
class HdropConverter : public QWindowsMimeConverter {
public:
    bool canConvertFromMime(const FORMATETC& formatetc, const QMimeData* mimeData) const override {
        return formatetc.cfFormat == CF_HDROP && (formatetc.tymed & TYMED_HGLOBAL)
            && mimeData && mimeData->hasFormat(QLatin1String(kMime));
    }

    bool convertFromMime(const FORMATETC& formatetc, const QMimeData* mimeData,
                         STGMEDIUM* pmedium) const override {
        if (!canConvertFromMime(formatetc, mimeData) || !pmedium) return false;
        // DROPFILES header, then each path NUL-terminated as UTF-16 with
        // native separators, then one more NUL to close the list.
        QString block;
        for (const QString& p : localPaths(mimeData)) {
            block += QDir::toNativeSeparators(p);
            block += QChar(0);
        }
        block += QChar(0);
        const size_t bytes = sizeof(DROPFILES) + size_t(block.size()) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!h) return false;
        void* mem = GlobalLock(h);
        if (!mem) { GlobalFree(h); return false; }
        auto* df = static_cast<DROPFILES*>(mem);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        memcpy(static_cast<char*>(mem) + sizeof(DROPFILES),
               block.utf16(), size_t(block.size()) * sizeof(wchar_t));
        GlobalUnlock(h);
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = h;
        pmedium->pUnkForRelease = nullptr;
        return true;
    }

    QList<FORMATETC> formatsForMime(const QString& mimeType, const QMimeData* mimeData) const override {
        QList<FORMATETC> out;
        if (mimeType != QLatin1String(kMime) || !mimeData) return out;
        FORMATETC f;
        f.cfFormat = CF_HDROP;
        f.ptd = nullptr;
        f.dwAspect = DVASPECT_CONTENT;
        f.lindex = -1;
        f.tymed = TYMED_HGLOBAL;
        out.append(f);
        return out;
    }

    bool canConvertToMime(const QString&, IDataObject*) const override { return false; }
    QVariant convertToMime(const QString&, IDataObject*, QMetaType) const override { return QVariant(); }
    QString mimeForFormat(const FORMATETC&) const override { return QString(); }
};

}  // namespace

void installNativeConverter() {
    // Registers itself with Qt's registry in the constructor and stays for
    // the life of the process; Qt only needs it to outlive the app.
    static HdropConverter* converter = nullptr;
    if (!converter) converter = new HdropConverter();
}
#else
void installNativeConverter() {}
#endif

}  // namespace DragPaths
