#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>

class QString;

// Facts about image formats that Qt won't tell us, in one place.
//
// -- Alias extensions ------------------------------------------------------
// Extensions that name an image format Qt can already decode but which no
// installed plugin advertises. Qt's jpeg plugin reports only "jpg"/"jpeg",
// yet a .jfif file is byte-identical to a .jpg — JFIF is the standard
// container for JPEG data, and some Windows download paths and "Save for
// Web" exporters simply pick the other suffix.
//
// Decoding never needs any of this: the viewer and the thumbnail workers
// both hand a QBuffer to QImageReader, which sniffs the format from the
// magic bytes and ignores the name. It matters only where a filename is
// all we have — classifying a directory entry as an image, and telling
// QImageWriter what to encode.
namespace ImageFormats {

// The format name QImageReader / QImageWriter knows `extension` by, or an
// empty array when `extension` is not an alias. Case-insensitive.
QByteArray aliasedFormat(const QString& extension);

// Every alias extension whose underlying format appears in
// `supportedFormats`, so an alias only surfaces when its decoder is
// actually installed.
QStringList aliasExtensionsFor(const QList<QByteArray>& supportedFormats);

// -- Lossy formats ---------------------------------------------------------
// True when `nameOrExtension` denotes a format whose encoder discards detail,
// so every re-save costs a little quality. Callers use it to decide whether to
// offer a quality control, whether to pass that quality to QImageWriter, and
// whether to warn before overwriting an original. Case-insensitive; accepts
// either an extension or a QImageWriter format name (they only differ for the
// aliases above, and both spellings are covered).
//
// Qt can't answer this: QImageWriter::supportsOption(Quality) reports whether
// the plugin *accepts* a quality value, and the PNG handler does — it maps the
// number onto a zlib compression level, which is lossless.
bool isLossy(const QString& nameOrExtension);

}
