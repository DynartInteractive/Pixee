# Future improvements

Open ideas and external-dependency paths that aren't urgent enough to plan out
yet. Each section is self-contained — pick one off and turn it into a real plan
when it becomes interesting.

## Format support: PSD, AI, and friends

Qt itself ships no native support for `.psd` or `.ai`. Two different paths,
because they're very different file formats.

### PSD — via the `kimageformats` plugin

The KDE community maintains a Qt image-format plugin set called
**kimageformats** that adds drop-in support for: PSD, AVIF, EXR, HDR, JXL, JP2,
KRA, ORA, PCX, PIC, QOI, RAS, RGB, SGI, TGA, XCF, JXR, and more.

Pixee already builds its accepted-extensions set from
`QImageReader::supportedImageFormats()` (in `Config::_setUpImageExtensions`), so
**no code changes are required** — once the plugin is installed against the
build's Qt kit, `psd` (and the others) start appearing in that set
automatically. The startup `qDebug() << "Supported image formats:"` line will
confirm it.

How to install on Windows MinGW (rough):

- Build from source: `git clone https://invent.kde.org/frameworks/kimageformats`
  → CMake against this kit's Qt 6.11 → install. The output `qpsd.dll` (and
  friends) lands in `C:\Qt\6.11.0\mingw_64\plugins\imageformats\`.
- Or via `vcpkg`: `vcpkg install kimageformats:x64-mingw-dynamic` (subject to
  triplet availability).

### AI — no off-the-shelf plugin; two viable approaches

`kimageformats` doesn't cover `.ai`. Two options:

1. **Qt PDF module (`QT += pdf`).** Modern Illustrator files (CS+) are PDF
   under the hood (they start with `%PDF-`). Render page 0 with
   `QPdfDocument` and rasterize at the thumbnail target size. Same code path
   would extend to actual `.pdf` files for free. Cost: an extra Qt module
   dependency and a `~40-line` branch in `ThumbnailWorker::process` that
   detects `.ai` / `.pdf` extensions and bypasses `QImageReader`.

2. **Extract the embedded JPEG preview.** Almost every AI file carries a
   compatibility-preview JPEG near the start. Scan the first ~256 KB for the
   SOI marker (`0xFFD8`), find the matching EOI (`0xFFD9`), hand the bytes to
   `QImage::loadFromData`. About 20 lines, no new Qt modules, reuses the
   worker's existing chunked-read path. Caveat: vector-only files without a
   preview wouldn't get a thumbnail.

Recommendation: option 2 first — small, contained, covers the realistic case.
Switch to option 1 if/when vector-faithful rendering becomes important.

### Where the integration would land

Whichever AI path is chosen, the natural seam is `ThumbnailWorker::process`:
detect the extension first, branch into a custom decoder if matched, fall
through to the existing `QImageReader` path otherwise. The result is still a
`QImage` that flows back through the cache + delegate the same way every other
thumbnail does — no other component needs to know.
