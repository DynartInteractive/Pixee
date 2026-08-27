# Adding JPEG XL (`.jxl`) support

Status: **planned, not started.** Written up on a machine without vcpkg so the
plugin half can be done on the machine that has it.

The short version: **the app code is almost ready.** Everything format-related
derives from `QImageReader::supportedImageFormats()` at startup, so browsing,
thumbnailing and viewing `.jxl` costs zero lines of C++ once a decoder plugin
is installed. The real work is (a) building/bundling that plugin, and (b) a
small cleanup so the *write* side treats JXL as the lossy format it is.

---

## Part 1 — What comes for free

`Config::_setUpImageExtensions()` (`src/Config.cpp:88`) builds the whole
accepted-extension set from `QImageReader::supportedImageFormats()`, and the
Save As dropdown from `QImageWriter::supportedImageFormats()`. Install a plugin
that advertises `jxl` and these all start working with no code change:

- `FileModel` classifies `.jxl` entries as `FileType::Image`
  (`src/FileModel.cpp:252`, fed from `Config::imageExtensions()`).
- Thumbnails, the viewer, the folder index-image overlay.
- Copy / Move / Rename / Delete / Batch rename — all path-based, format-blind.
- The Metadata panel's Qt-derived basics (format, dimensions, embedded text).
- `jxl` appears in the Save As format dropdown, **if** the plugin also writes.

The decode paths never look at the filename: `ThumbnailWorker` and `ImageLoader`
both hand a `QBuffer` to `QImageReader`, which sniffs the magic bytes. So there
is nothing to register, and **no `ImageFormats` alias entry is needed** — `.jxl`
is the format's only suffix and the plugin advertises it (the alias table exists
for suffixes no plugin claims, like `.jfif`).

---

## Part 2 — The code change: one lossy-format set

JPEG XL is lossy by default. The lossy set is currently **hardcoded in five
places**, and none of them know about `jxl`:

| Site | Consequence of omitting `jxl` |
|---|---|
| `SaveAsDialog::formatIsLossy` (`src/SaveAsDialog.cpp:101`) | quality slider hidden; the dialog's `quality()` is still passed on, so the writer gets an arbitrary value the user never saw |
| `SaveImageTask::run` (`src/SaveImageTask.cpp:68`) | `writer.setQuality()` skipped — plugin default silently applied |
| `ConvertFormatTask::run` (`src/ConvertFormatTask.cpp:68`) | same (this one is `jpg`/`jpeg` only — it already misses WebP) |
| `ScaleImageTask::run` (`src/ScaleImageTask.cpp:90`) | same (also `jpg`/`jpeg` only) |
| `MainWindow::saveImage` (`src/MainWindow.cpp:875`) | **the "re-saving is lossy, use Save As" warning never shows** when overwriting a `.jxl` original with `Ctrl+S` |

That last one is the only genuinely user-visible defect, and it is the
data-loss-shaped one. Note the table also records two pre-existing
inconsistencies: `ConvertFormatTask` and `ScaleImageTask` never set quality for
WebP even though `SaveAsDialog` offers a slider for it.

### Suggested shape

Put the predicate next to the other format facts, in `ImageFormats` — it is
already the "things about formats that Qt won't tell us" home, and
`ScaleImageTask` includes it:

```cpp
// ImageFormats.h
// True when `extension` names a format whose encoder throws away detail, so
// the caller should offer a quality control and warn before overwriting an
// original. Case-insensitive; accepts an extension or a format name.
bool isLossy(const QString& extension);
```

```cpp
// ImageFormats.cpp
constexpr const char* kLossyFormats[] = { "jpg", "jpeg", "jfif", "webp", "jxl" };
```

Then all five sites become `ImageFormats::isLossy(...)`. Add a case to
`tests/ImageTasks` (`tst_ImageTasks.cpp`) covering the set — that suite already
exercises Scale/Convert quality behaviour.

Roughly 30 lines plus the test. Worth doing **before** the plugin lands: it
fixes the WebP quality gaps on its own merits, and makes JXL a genuine drop-in.

### One semantic wrinkle to check

The KDE JXL plugin appears to map `QImageWriter` quality **100 to
mathematically lossless** (distance 0), unlike JPEG where 100 is still lossy.
Verify this against the built plugin. If it holds, the Save As quality row and
the overwrite warning want JXL-specific wording rather than reusing the JPEG
copy — "100 = lossless" is a genuinely useful thing to surface, and the warning
is arguably wrong at quality 100.

---

## Part 3 — The plugin (do this on the vcpkg machine)

`docs/windows-extra-image-formats.md` is the recipe, and it already covers JXL
implicitly: gotcha 5 notes that *"Other formats (AVIF, PSD, XCF, JXL, …)
auto-enable based on dependency presence."* So kimageformats builds
`kimg_jxl.dll` as soon as `libjxl` is findable.

1. `vcpkg install libjxl` (on top of the existing AVIF/HEIF set).
2. Rebuild kimageformats per Stage 3 of that doc. **Use `--clean-first`** —
   gotcha 6 is about codec availability being cached in a `static const bool`
   that a plain rebuild doesn't refresh; the same trap applies here.
3. Confirm the plugin actually advertises the format. The failure mode to watch
   for (we hit it with AVIF) is *plugin loads fine, format still missing from
   `supportedImageFormats()`* because the plugin's `capabilities()` filtered it
   at runtime. The startup log's `Supported image formats:` line is the check.
4. Refresh `thirdparty/imageformats/`:
   - `kimg_jxl.dll` → `plugins/`
   - its codec DLLs → `codecs/`. Expect `jxl.dll`, `jxl_threads.dll`,
     `jxl_cms.dll`, `brotlicommon.dll`, `brotlidec.dll`, `brotlienc.dll`,
     `hwy.dll` — but take the actual list from
     `Dependencies.exe -modules kimg_jxl.dll` rather than this guess. A missing
     transitive DLL reproduces the `jpeg62.dll`/`avif.dll` trap: the plugin
     silently drops out of the format list.
   - update `thirdparty/imageformats/README.md`'s Contents + Provenance.

**No build-script changes are needed.** `scripts/make-portable.bat:179` copies
`plugins\kimg_*.dll` and `codecs\*.dll` by wildcard, and
`scripts/make-portable.sh:225` deploys the whole `imageformats` plugin
directory. Both pick up JXL automatically.

### Linux

Same story as AVIF in the README: Debian/Ubuntu have no Qt 6 JXL package, so
it's either a distro `kimageformats` build or
[`qt-jpegxl-image-plugin`](https://github.com/novomesk/qt-jpegxl-image-plugin)
built against `qmake6`, dropped into the system `qt6/plugins/imageformats`.
`make-portable.sh` then bundles it with no edit.

---

## Part 4 — Verification checklist

- [ ] Startup log's `Supported image formats:` includes `jxl`.
- [ ] `.jxl` files show as images in the grid, with thumbnails.
- [ ] Viewer opens one at full res; rotate/flip/crop then Save works.
- [ ] `Ctrl+S` over a `.jxl` original shows the lossy warning.
- [ ] Save As offers `jxl` **with** a quality slider (i.e. the writer half of
      the plugin is present — read-only would list nothing here).
- [ ] Metadata panel shows format/dimensions; check whether EXIF appears (see
      below).
- [ ] Portable build: `Pixee-portable\imageformats\kimg_jxl.dll` present and the
      format still listed when run on a machine without the codecs on `PATH`.

---

## Part 5 — Known unknowns

- **Thumbnail speed.** `ThumbnailWorker`'s fast path (`findExifThumbnail`,
  `src/ThumbnailWorker.cpp:42`) is JPEG-only — it checks for the SOI marker — so
  JXL always takes the full decode. How much that hurts on a share depends on
  whether the plugin honours `reader.setScaledSize()`
  (`src/ThumbnailWorker.cpp:267`); if it doesn't, a 40MP `.jxl` fully decodes
  before being scaled down. Measure before deciding it needs anything. JXL does
  carry a cheap DC preview in the codestream, but reaching it means bypassing Qt
  for libjxl directly — not worth it unless the numbers are bad.
- **Exiv2 / EXIF.** Exiv2 0.28 reads BMFF containers, so a container-form `.jxl`
  should yield EXIF. A **bare codestream** (starts `FF 0A`) has no container and
  therefore no EXIF at all — the panel will correctly show only the Qt basics.
  Confirm rather than assume; note it in `docs/metadata.md` if it's worth
  documenting.
- **Write support.** Confirm `kimg_jxl` registers as a writer, not just a
  reader. If it is read-only, `jxl` simply never appears in Save As — and the
  Part 2 work still stands, since the `Ctrl+S` warning matters either way.

---

## Part 6 — Docs to update once it works

- `README.md:33` — the format-support bullet's example list.
- `README.md:38` — Save/Save As says *"a quality slider for the lossy ones
  (JPEG / WebP)"*.
- `README.md:118–125` — the extra-image-formats section (both the Windows
  prebuilt list and the Linux notes).
- `thirdparty/imageformats/README.md` — Contents + Provenance.
- `docs/windows-extra-image-formats.md` — add `libjxl` to the vcpkg install line
  so the recipe reproduces the JXL build too.
