#pragma once

class QImage;
class QImageReader;
class QString;

namespace IcoUtils {

// For multi-image ICO files, decode every sub-image and return the
// best candidate (largest area; ties broken by higher pixel depth).
// Returns a null QImage when the path isn't an ICO or the file only
// has one entry — caller falls back to reader.read() in that case.
//
// Mutates the reader's position. Safe to call once before the normal
// reader.read() path: on a null return the reader is untouched.
QImage pickBestSubImage(QImageReader& reader, const QString& path);

}
