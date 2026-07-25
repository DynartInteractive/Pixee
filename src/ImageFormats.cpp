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

}
