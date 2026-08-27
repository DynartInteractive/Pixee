#include "ImageFormats.h"

#include <QLatin1String>
#include <QString>

namespace {

struct Alias {
    const char* extension;
    const char* format;
};

// Far too small for the lookup strategy to matter; a linear scan keeps the
// table readable as the single place new aliases get added.
constexpr Alias kAliases[] = {
    { "jfif", "jpeg" },
};

// Both the extension and the QImageWriter format name for each lossy format,
// so callers can pass whichever they hold without normalising first. "jxl" is
// listed ahead of the plugin that decodes it — an entry here costs nothing
// until a format shows up in supportedImageFormats(), and this way JXL lands
// with its quality handling already correct (docs/jpeg-xl-support.md).
constexpr const char* kLossyFormats[] = { "jpg", "jpeg", "jfif", "webp", "jxl" };

}

namespace ImageFormats {

QByteArray aliasedFormat(const QString& extension) {
    const QString needle = extension.toLower();
    for (const Alias& alias : kAliases) {
        if (needle == QLatin1String(alias.extension)) {
            return QByteArray(alias.format);
        }
    }
    return QByteArray();
}

QStringList aliasExtensionsFor(const QList<QByteArray>& supportedFormats) {
    QStringList result;
    for (const Alias& alias : kAliases) {
        if (supportedFormats.contains(QByteArray(alias.format))) {
            result << QString::fromLatin1(alias.extension);
        }
    }
    return result;
}

bool isLossy(const QString& nameOrExtension) {
    const QString needle = nameOrExtension.toLower();
    for (const char* format : kLossyFormats) {
        if (needle == QLatin1String(format)) return true;
    }
    return false;
}

}
