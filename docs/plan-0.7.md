# Plan: the 0.7 features

Five features chosen from [`compare-xnviewmp.md`](compare-xnviewmp.md) as the
ones a photo-folder user misses first, plus one small optional sixth. Video
thumbnails are explicitly out: they are the thing that makes XnView MP crawl
on a mixed folder, and Pixee is a photo tool.

Each section is self-contained: what the user gets, how it hangs off the
existing code, what settings and tests it adds, and what it costs. The order at
the end is a suggested build order, not a ranking of importance.

| # | Feature | Size | Needs |
|---|---|---|---|
| 1 | Name filter in the current folder | small | nothing |
| 2 | Slideshow | small | nothing |
| 3 | Colour management (embedded ICC → sRGB) | small–medium | nothing |
| 4 | Metadata write: lossless rotate, EXIF kept on save | medium | Exiv2 (already shipped) |
| 5 | RAW support | medium, plus a plugin build | Exiv2 for previews; kimageformats + LibRaw for full decode |
| 6 | Resize on Save As (optional) | small | nothing |

The constraints from `CLAUDE.md` apply throughout: everything must fit at
1280×720, pure logic gets a test suite under `tests/`, README / CHANGELOG /
docs are updated in the same commit as the code, and settings keys live in
`AppSettings.h`.

---

## 1. Name filter

### What the user gets

`Ctrl + F` while browsing opens a slim filter bar under the path bar: one text
field and a close button. Typing hides every cell whose name does not contain
the text (case-insensitive). `*` and `?` work as wildcards when present. `Esc`
in the field, or the close button, clears the filter and hides the bar. The
status bar shows *12 of 340* while a filter is active. The filter is dropped
when you enter another folder: it is a quick look, not a saved view.

`..` is always shown. Folders are filtered like files, so typing `2024` in a
year-structured archive shows just those folders.

The viewer, when opened from a filtered grid, walks only the visible images,
so a filter plus a slideshow (feature 2) is a cheap "play these" command.

### Design

- `FileFilterModel` gains `setNameFilter(const QString&)` and applies it in
  `filterAcceptsRow` after the type check. The matching itself goes into a
  pure helper, `NameFilter::matches(name, pattern)` in `src/NameFilter.{h,cpp}`:
  substring when the pattern has no wildcard, `QRegularExpression::wildcardToRegularExpression`
  otherwise, always case-insensitive. Pure so it can be tested without a model.
- `MainWindow` owns the bar (a `QWidget` with a `QLineEdit` and a flat close
  button, hidden by default) inserted into the central layout between the path
  bar and `_centerStack`. A `QShortcut(Ctrl+F)` on the file list shows and
  focuses it. `navigateTo` clears it.
- Verify that `activateImage` builds its image list from the proxy model, not
  `FileModel`; if it reads the source model, switch it to the proxy so the
  viewer and the slideshow respect the filter.
- Status bar count: the proxy's `rowCount` vs the source's, minus `..`.

### Settings, docs, tests

- No settings.
- Tests: `tests/NameFilter` covering substring, case, wildcards, empty pattern.
- README: keyboard table (`Ctrl + F`), one feature bullet. CHANGELOG Added.

### Density

One extra 26 px row, only while the filter is open. Nothing is added to the
context menus.

---

## 2. Slideshow

### What the user gets

`View → Slideshow` (`Shift + F11`) starts a slideshow from the current image
in the viewer, or from the first selected image (else the first image) while
browsing. It goes fullscreen, advances every *N* seconds, wraps at the end, and
a toast at the start says *Slideshow, 3 s. Space pauses, Esc ends.*

- `Space` pauses and resumes. `←` / `→` step and restart the interval.
- `Esc` ends the slideshow, leaves fullscreen and returns to wherever the
  user started (viewer or grid).
- Any edit key (rotate, flip, crop, adjust) ends the slideshow without
  leaving the viewer, so an edit is never interrupted by an advance.
- The interval counts from the moment the full-resolution image has
  arrived, not from the request, so a slow share never shows a placeholder
  for the whole slot.

### Design

- `MainWindow`: `_slideshowTimer` (single shot), `startSlideshow()`,
  `stopSlideshow()`, `slideshowActive()`. `viewerNext()` gets a `wrap`
  argument (today it stops at the last image). The timer is armed in the
  `ImageLoader::loaded` handler when the loaded path is the current one and
  the slideshow is active.
- `ViewerWidget`: `Space` is currently the pan modifier. In slideshow mode
  the viewer is always in a fit mode, where Space does nothing, so a new
  `pauseToggled()` signal on a Space press while `_slideshowMode` is set is
  enough. The flag also makes edit keys emit `editAttempted()` so the main
  window can stop the show first.
- `enterFullscreen` / `exitFullscreen` are reused; `stopSlideshow` restores
  the pre-slideshow fullscreen state and the central page.
- The 5-image preload cache already makes the advance instant on local
  disks; nothing new is needed there.

### Settings, docs, tests

- `viewer/slideshowInterval` (int seconds, 1–60, default 3) and
  `viewer/slideshowWrap` (bool, default true) on the Viewer settings page.
  Shuffle is deliberately left out of this round.
- No tests (UI only); the wrap arithmetic is one line.
- README: feature bullet, keyboard table rows (`Shift + F11`, `Space`).
  CHANGELOG Added. `docs/image-viewer-plan.md` lists slideshow under
  non-goals; strike that line.

### Density

One View-menu row, two settings rows. The Viewer settings page has three
rows today, so five is fine.

---

## 3. Colour management

### What the user gets

An image with an embedded colour profile (Adobe RGB or ProPhoto from a camera
or Lightroom, Display P3 from a phone) shows with correct colours instead of
flat and desaturated. Thumbnails, the viewer, the histogram and anything
saved from the viewer all agree. Images without a profile are treated as sRGB,
which is what they are in practice.

This round targets sRGB. A monitor profile (for wide-gamut displays) is a
follow-up; see *Later* below.

### Design

Qt already decodes the profile: `QImageReader` sets `QImage::colorSpace()`
from PNG `iCCP` and JPEG APP2 ICC chunks, and Qt's own writers embed the
colour space back when they save. So the whole feature is one conversion in
the right places:

- New pure helper `ColorManagement::toSRgb(const QImage&)` in
  `src/ColorManagement.{h,cpp}`: returns the input untouched when it has no
  valid colour space or is already sRGB, else `convertedToColorSpace(QColorSpace::SRgb)`
  (which keeps the format, including 16-bit ones). Pure, tested.
- Call sites, all off the GUI thread:
  - `ImageLoader::load` after decode (the one full-size conversion, roughly
    100–200 ms for 24 MP, acceptable in the worker).
  - `ThumbnailWorker::processImpl` after scaling to the thumbnail, not
    before, so the conversion touches 256 px of pixels, not 24 MP. The
    embedded EXIF thumbnail path has no profile and needs nothing.
  - `PreviewReader::read` for the browsing histogram, so it does not
    disagree with the viewer.
- `ViewerWidget` works in sRGB from then on. `editedImage()` carries the sRGB
  colour space, so Save / Save As write an sRGB file with the sRGB profile
  embedded, which is the correct result of "convert to sRGB".
- A setting turns the conversion off for anyone who wants raw pixels.

### Settings, docs, tests

- `viewer/colourManagement` (bool, default true): *Convert images with an
  embedded colour profile to sRGB.* Applied live for the viewer; thumbnails
  already cached are unaffected, which the setting's tooltip should say.
- Tests: `tests/ColorManagement`: a synthetic Adobe RGB image (primaries via
  `QColorSpace(QColorSpace::Primaries::AdobeRgb, …)`) converts to known sRGB
  values within tolerance; an image with no colour space is returned as the
  same shared buffer; 16-bit input stays 16-bit.
- README: feature bullet. CHANGELOG Added. Update the `Pixel-art aware`
  bullet's neighbour, since this is the first colour-correctness feature.

### Later (not this round)

Monitor profiles. On Windows the display's ICC file comes from
`GetICMProfileW` on the window's monitor and loads with
`QColorSpace::fromIccProfile`; Linux has no portable answer (X11 `_ICC_PROFILE`
atom, nothing on Wayland). The display transform should then be applied to
the viewer's screen-resolution proxy only, never baked into saves, which fits
the existing `_previewSource` / `_preview` pipeline. Worth doing once someone
with a wide-gamut monitor asks.

---

## 4. Metadata write: lossless rotate, EXIF kept on save

### What the user gets

- Rotating or flipping a JPEG and pressing `Ctrl + S` no longer re-encodes
  it. Pixee writes the EXIF orientation tag, the way the camera itself does,
  so the file keeps its original bytes and quality.
- Saving any other edit (crop, colour, or a rotate on a format Exiv2 cannot
  tag) still re-encodes, but now **keeps the camera metadata**. Today a
  re-encoded save silently strips EXIF, IPTC and XMP, because Qt's writers
  know nothing about them; this is the larger bug of the two and comes for
  free with the same machinery.
- Both only in the Exiv2 build (the shipped Windows binaries). Without
  Exiv2 the behaviour is unchanged.

### Design

**Metadata writer.** New `src/MetadataWriter.{h,cpp}`, Exiv2 contained the
same way `MetadataReader.cpp` contains it, behind `PIXEE_HAVE_EXIV2`:

- `bool setOrientation(const QString& path, int orientation, QString* error)`:
  read metadata, set `Exif.Image.Orientation`, write. Drops the EXIF
  thumbnail's own orientation confusion by leaving the thumbnail alone;
  every viewer applies the same tag to it, including Pixee's own
  `applyExifOrientation`.
- `bool copyMetadata(const QString& from, const QString& to, QSize newSize, QString* error)`:
  copy EXIF, IPTC and XMP from the original to the freshly written file,
  set `Exif.Image.Orientation` to 1 (the pixels are now upright),
  update `Exif.Photo.PixelXDimension` / `PixelYDimension`, and drop the
  embedded thumbnail (it would show the old framing after a crop). Exiv2
  can write into JPEG, TIFF, PNG, WebP; for other outputs it returns false
  and the save is still a success, with a log line.
- Formats Exiv2 cannot tag at all (GIF, BMP, SVG) fall back to re-encode.

**Orientation algebra.** Pure `Orientation` namespace in
`src/Orientation.{h,cpp}`: `compose(int existing, int delta)` over the EXIF
1–8 group, plus `fromTransform(rotate90s, flipH, flipV)`. Tested against the
full 8×8 table.

**Viewer.** `ViewerWidget` keeps baking rotate / flip into `_baked` for
display (nothing changes on screen), but additionally records the geometry
edit as a *transform delta*: a rotation count and flip flags, reset by crop
(a crop makes the bake unavoidable) and by `setImage`. New accessor
`pendingTransform()` returning the delta and whether it is *orientation only*
(no crop, no colour adjustment).

**Save.** `MainWindow::saveImage`:

1. If Exiv2 is present, the pending edit is orientation-only, and the source
   format is one Exiv2 writes orientation into (JPEG, TIFF, WebP, PNG):
   compute `compose(existingOrientation, delta)` using the orientation the
   `MetadataReader` already reports in `ImageMetadata::orientation`, and
   run a new `SetOrientationTask` (task pipeline, so the progress dock and
   the `pathTouched` refresh work). `markSaved` with the current pixels.
   The thumbnail cache invalidates itself through the changed mtime.
2. Otherwise re-encode through `SaveImageTask` as today, then call
   `copyMetadata` from the task's `run()` once the bytes are on disk.
   `SaveImageTask` gets an optional `metadataSource` path for this;
   `ConvertFormatTask` gets the same for Save As without edits.

The confirm prompt before overwriting a lossy original should say which of
the two is about to happen: *Save rotation without re-encoding* vs *Re-encode
and overwrite*.

### Settings, docs, tests

- No new settings. (A "always re-encode" switch is not worth a row; Save As
  already gives a re-encoded copy.)
- Tests: `tests/Orientation` (the composition table, round trips),
  `tests/Metadata` extended with a write round trip: set orientation, read
  back; copy metadata to a re-encoded file, check camera fields survive and
  orientation is 1.
- README: update the in-viewer editing bullet (remove the "re-encodes for
  now" caveat), the Save bullet (metadata is kept). `docs/metadata.md`: a
  *Writing* section. CHANGELOG Added and Fixed (the EXIF stripping).

### Risks

- Exiv2 writing to a file on an SMB share rewrites the whole file; it
  should go through the same "write beside, then rename" discipline as
  `ImageFormats::writeImage`. Exiv2 does this itself for JPEG (temp file +
  rename), worth confirming on a share before shipping.
- Tools that ignore the orientation tag show the image unrotated. Windows
  Explorer, every browser, macOS and all current photo tools honour it; the
  README should mention it once.

---

## 5. RAW support

### What the user gets

Camera RAW files (`.cr2`, `.cr3`, `.nef`, `.arw`, `.dng`, `.orf`, `.rw2`,
`.raf`, `.pef`, `.srw` and the rest) appear in the grid with thumbnails,
open in the viewer, show their EXIF in the Metadata panel, and can be
exported with Save As. `File → Save` is disabled for them, since there is no
RAW encoder.

Two levels, chosen in Settings → Viewer, *RAW files*:

- **Embedded preview** (default): the viewer shows the JPEG the camera
  stored inside the RAW. It is typically full or half resolution, carries the
  camera's own rendering, and loads in the time a JPEG takes. This is what
  most viewers, XnView MP included, show by default.
- **Full decode**: demosaic the sensor data (LibRaw via the kimageformats
  `raw` plugin). Slower by seconds on a 24 MP file and rendered with
  LibRaw's generic camera profile, but it is the real pixels for cropping
  and adjusting. Only offered when the plugin is installed; the setting
  says so when it is not.

Thumbnails always come from the embedded preview.

### Design

**Preview path (Exiv2, already shipped).** Exiv2's `PreviewManager` lists
and extracts the embedded JPEG previews from every major RAW container (CR2,
CR3, NEF, ARW, DNG, ORF, RW2, RAF, PEF). This gives thumbnails and the
default viewer image with no new dependency:

- `src/RawPreview.{h,cpp}` behind `PIXEE_HAVE_EXIV2`: `QByteArray largestPreview(const QByteArray& fileBytes, int* orientation)`,
  plus `QStringList RawPreview::extensions()` listing the RAW suffixes Exiv2
  handles. Exiv2 headers stay inside this file, like `MetadataReader.cpp`.
- `Config::_setUpImageExtensions` adds `RawPreview::extensions()` to the
  accepted set when Exiv2 is built in, the way `ImageFormats::aliasExtensionsFor`
  adds JFIF. Without Exiv2 and without the plugin, RAW files stay invisible
  as today.
- `ThumbnailWorker::processImpl`: for a RAW extension, phase 2 (the full read)
  hands the bytes to `largestPreview`, decodes that JPEG with `QImageReader`,
  applies the orientation and continues into the normal scale-and-cache
  path. The 512 KB phase-1 probe is skipped for RAW: the big preview sits
  mid-file in NEF and ARW, so the whole file is needed anyway. Chunked read
  and abort work unchanged.
- `ImageLoader::load`: for a RAW extension in *Embedded preview* mode, the
  same extraction, then the normal decode of the preview bytes. The viewer
  does not know it is looking at a preview, except through a status-bar
  note (*RAW, embedded preview 3008×2000*) so a user cropping it knows it
  is not the sensor resolution.
- `MetadataReader` already parses RAW EXIF through Exiv2; nothing to do.
- `MainWindow::saveImage`: disable Save when `ImageFormats::canWrite` is
  false for the source, which also covers GIF and SVG today (check whether
  this is already handled; if not, it is a one-line fix with its own test).

**Full decode (kimageformats `raw` plugin).** kimageformats builds
`kimg_raw` when LibRaw is found at configure time; it registers the RAW
suffixes with `QImageReader`, so `Config` picks them up with no change, and
`ImageLoader` decodes through `QImageReader` as for any other format. The
plugin outputs sRGB, so feature 3 needs no RAW branch. Pixee-side work is
only the setting that chooses between the two paths. The build work:

- Windows: `vcpkg install libraw --triplet x64-windows`, reconfigure
  kimageformats (the recipe in `docs/windows-extra-image-formats.md`) and
  confirm `raw` in the configure summary. `kimg_raw.dll` lands in
  `imageformats/`, `raw.dll` next to the exe; `scripts/make-portable.bat` and
  the installer script need the new DLL in their copy lists. Licence: LibRaw
  is LGPL 2.1 / CDDL, no new obligations beyond the attribution folder.
- Flatpak: check whether the KDE runtime's kimageformats ships `kimg_raw`;
  if not, add `libraw` and a kimageformats module to the manifest. Note in
  `docs/flatpak.md`.
- Linux from source: distribution package `kimageformat-plugins` (Debian) or
  `kimageformats` (Arch) usually includes it; document.

### Settings, docs, tests

- `viewer/rawDecode` ("preview", the default, or "full") on the Viewer page.
  The "full" entry is disabled with a tooltip when no RAW suffix is in
  `QImageReader::supportedImageFormats()`.
- Tests: `tests/RawPreview` with one small real RAW per container family
  checked into `tests/data/` (a DNG from a phone is small; CR2 and NEF
  samples exist under free licences from the LibRaw and Exiv2 test suites,
  trimmed to the preview). Assert a preview is found, is a JPEG, and the
  orientation is read.
- README: Format support bullet, Settings bullet, Save bullet. New
  `docs/raw.md` with the two levels, the plugin build, and a list of
  containers tried. `docs/windows-extra-image-formats.md`: LibRaw stage.
  CHANGELOG Added.

### Risks

- CR3 is a newer container; Exiv2 supports its previews from 0.27.4 (Pixee
  uses 0.28.8), LibRaw from 0.20. Fine on both counts, but test one.
- Some previews are small (older Olympus and Pentax bodies store 160×120
  plus a 1/4-size one). The viewer should fall back to full decode when the
  largest preview is below, say, 800 px on the long edge and the plugin is
  present, else show it scaled with the status note.
- Adobe RGB previews: a camera set to Adobe RGB writes an Adobe RGB JPEG
  preview with no ICC profile, only `Exif.Photo.ColorSpace = 2` or the
  `Uncalibrated` + `InteropIndex R03` pair. Feature 3 should treat that EXIF
  marker as Adobe RGB, for JPEGs as well. Small addition to
  `ColorManagement::toSRgb` taking the EXIF value as a hint.

---

## 6. Resize on Save As (optional)

### What the user gets

The Save As dialog gains a *Resize* row: **No**, **Long edge** with a pixel
box, or **Percent**. The target-path line also shows the resulting size
(*→ 1600 × 1067*). The last choice is remembered. Upscaling is allowed but
the dialog says so in the readout.

### Design

- Pure `ResizeSpec` in `src/ResizeSpec.{h,cpp}`: `{ Mode mode; int value; }`
  and `QSize targetFor(QSize source)`; tested.
- `SaveAsDialog` gets the row and returns a `ResizeSpec`. `SaveImageTask` and
  `ConvertFormatTask` take an optional spec and scale with
  `Qt::SmoothTransformation` before encoding (`ScaleImageTask` already exists;
  reuse its scaling helper rather than adding a third copy).
- Combined with feature 4, a resized export still carries the metadata and
  updated pixel dimensions.

### Settings, docs, tests

- `fileOps/lastSaveAsResize` (string, dialog memory only, not on a settings
  page).
- Tests: `tests/ResizeSpec`, and one case in `tests/ImageTasks` writing a
  resized file.
- README: Save / Save As bullet. CHANGELOG Added.

---

## Suggested order

1. **Name filter** and **Slideshow**. Two short days, no dependencies, both
   visible on the website at once. Ship a test build after each.
2. **Colour management**. Small, and it should be in place before RAW
   previews arrive, which exercise the Adobe RGB hint.
3. **Metadata write**. The EXIF-stripping fix matters even to people who
   never rotate.
4. **RAW, preview path.** Everything except full decode, with no new
   dependency. This is the point at which RAW files show up in the grid.
5. **RAW, full decode.** The vcpkg LibRaw and kimageformats rebuild can run
   on the side while 1–4 are coded; once `kimg_raw.dll` loads, the remaining
   Pixee work is the setting.
6. **Resize on Save As**, if there is appetite left.

Steps 1–4 are a coherent 0.7.0 on their own; 5 can slip to 0.7.1 without
anyone noticing, because the preview path already makes RAW folders usable.

## Not in this round

Tabs, categories and ratings, batch convert, compare view, duplicate finder,
monitor profiles, shuffle in the slideshow, lossless pixel rotation through
jpegtran (the orientation tag covers the real use), and video of any kind.
