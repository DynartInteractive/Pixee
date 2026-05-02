#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <QString>

// Shared test fixtures and factories. Compiled into each test binary's
// .pro via SOURCES += ../TestHelpers.cpp; see tests/README.md (TODO)
// or a sibling subdir's .pro for the canonical example.
namespace TestHelpers {

// Writes `size` bytes of pseudo-random data to `path` (overwriting any
// existing file). Used by file-op tests that need a real file but don't
// care about its contents — useful for filling chunked-copy code paths.
void writeBytes(const QString& path, qint64 size);

// Writes a real image of the given dimensions to `path` via QImageWriter,
// so image-format tasks (Scale, Convert) have decode-able input.
// `format` is a QImageWriter format name ("png", "jpg", "webp", ...).
// The pixel content is a flat colour fill — tests that care about
// orientation/EXIF should write their own bytes via QImageWriter
// directly.
void writeImage(const QString& path, int width, int height,
                const char* format = "png");

}

#endif
