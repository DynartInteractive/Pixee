# Changelog

All notable changes to Pixee are documented here. The format is loosely based
on [Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/) (pre-1.0: features bump the minor,
fixes bump the patch).

## [0.4.0] — 2026-09-17

Linux packaging. Pixee can now be built and installed as a Flatpak, which also
gives it the desktop integration it never had on Linux: an app icon, a launcher
entry and app-store metadata. Nothing about the Windows build changes except
that the window finally carries an icon.

### Linux packaging

- **Flatpak** — `packaging/net.dynart.Pixee.yml` builds against the KDE 6.11
  runtime. `flatpak run org.flatpak.Builder --user --install --force-clean build
  packaging/net.dynart.Pixee.yml` and you have it; the manifest builds from the
  working tree, so no tag or commit is needed to try one.
- **More image formats, for free** — the KDE runtime bundles kimageformats, so
  the Flatpak reads HEIC/HEIF, AVIF, JPEG XL, PSD/PSB, XCF, KRA, ORA, DDS, QOI
  and SVG without the plugin juggling the Windows build needs.
- **Desktop integration** — a desktop entry with MIME associations (Pixee shows
  up in your file manager's *Open with*), AppStream metadata for software
  centres, and an app icon. The window is tied to its desktop entry via
  `setDesktopFileName`, which is what Wayland reads the icon from.
- **`make install`** — `Pixee.pro` now installs under `$$PREFIX` (default
  `/usr/local`) following the freedesktop layout, replacing the old
  `/opt/Pixee/bin` default.
- **Not on Flathub yet** — see [`docs/flathub-requirements.md`](docs/flathub-requirements.md)
  for what's left.

### Changed

- **The Linux data folder moved to the XDG location** —
  `~/.local/share/Dynart/Pixee` instead of `~/.pixee`. Your thumbnail cache is
  carried across automatically on first run, and a user theme in the old
  location keeps working. Windows is unchanged. This exists so the Flatpak,
  which is granted the host filesystem, keeps its state to itself instead of
  writing a dot-folder into your real home.
- **"Open with" inside a Flatpak** goes through the desktop's own application
  chooser rather than the configurable program list — in a sandbox those stored
  paths point at host binaries that aren't reachable. Unchanged everywhere else.

### Added

- **App icon** — used for the launcher, the software-centre listing, and the
  window itself on Windows and X11.

## [0.3.0] — 2026-09-15

### Colour adjustment

- **Adjust dock** (`View → Adjust`, or `Edit ▸ Adjust colours…` in the viewer) —
  live sliders for **brightness, contrast, saturation, hue and gamma**, applied
  to the viewer's image as you drag. Unlike rotate / flip / crop these are kept
  as *parameters* rather than baked pixels, so a slider never compounds on its
  own last value, dragging back to zero restores the original exactly, and the
  settings survive a rotation. Double-click a slider's label to reset just that
  one, **Reset all** for the lot, and hold **`B`** to compare against the
  original.
- The preview runs on a screen-resolution proxy, which is what keeps a drag
  responsive: a full 24 MP pass measures 174 ms, the proxy 15 ms. Zooming past
  1:1 falls back to full resolution rather than going soft, and the
  full-resolution pass happens once — when you save.
- Adjustments count as an unsaved edit, so `File → Save`, `Save As…` and the
  navigate-away prompt all cover them. The panel is viewer-only and greys out
  while browsing, since adjusting needs the full-resolution image.

### Histogram

- **Histogram dock** (`View → Histogram`) — RGB (additively blended, so overlaps
  read as the colour they actually make) or luminance, an optional **log scale**
  for when one flat expanse of sky would otherwise squash everything onto the
  axis, and a **clipping readout** showing how much of the image has been pushed
  off each end.
- In the viewer it reads the *same* pixels being painted, so it follows every
  adjustment as you drag rather than lagging a step behind, and it always covers
  the whole image — panning and zooming never reshape it. While browsing it
  follows the file list instead: one image selected reads that file off-thread;
  several, or none, empties the graph rather than leaving the last one up.

### File operations

- **`Del` / `Shift + Del` with the folder tree focused** now deletes the folder
  selected there — to the recycle bin or permanently — with the same
  confirmation and task pipeline as the file list, then steps the selection up
  to the parent.
- **`Ctrl + V` with the folder tree focused** pastes into the folder selected
  there rather than the folder being browsed.

### Interface

- The Metadata, Adjust and Histogram docks share the right-hand column,
  **stacked vertically** so an open histogram and metadata panel are visible at
  the same time. All three fit alongside the image at 1280×720.
- Dark theme: the new panels' labels, spin boxes and drop-downs are themed
  rather than falling back to near-black text on a dark ground.

### Fixed

- Folders now open scrolled to the top. `QListView::setRootIndex` re-roots
  without resetting the scroll offset, so a folder could open part-way down —
  or pinned to its bottom when it was shorter than the previous one.

## [0.2.0] — 2026-08-25

### Metadata

- **Metadata panel** (`View → Metadata`) — a read-only info dock for the focused
  image, updating both as you select thumbnails and as you navigate the viewer.
  Reads off-thread so it never stalls browsing on a network share, and does no
  disk I/O at all while the dock is hidden. Shows dimensions / format / size and
  any **embedded PNG text** — including AI-tool generation data (ComfyUI
  `prompt` / `workflow`, Automatic1111 `parameters`) — in every build; with the
  optional [Exiv2](https://exiv2.org/) backend it adds full **EXIF / IPTC / XMP**
  (camera, exposure, date taken, GPS, and a complete tag dump). Right-click or
  `Ctrl + C` copies the full, un-elided value — handy for lifting a long prompt.
  See [`docs/metadata.md`](docs/metadata.md) to enable Exiv2.

### Editing

- **In-viewer editing** — rotate (`R` / `Shift + R`), flip horizontal / vertical
  (`H` / `V`), and **crop** (`C`, then drag a rectangle — `Enter` applies, `Esc`
  cancels), from the viewer's `Edit ▸` context menu or the keys. Edits apply to
  an in-memory buffer layered over the loaded image, so nothing touches disk
  until you save. Rotate re-encodes the pixels for now; lossless
  orientation-only rotation waits on metadata *write* support.
- **Save** (`File → Save`, `Ctrl + S`) writes the edited pixels back over the
  original after a confirm, warning first when the target format is lossy
  (JPEG / WebP).
- **Save As** (`File → Save As…`, `Ctrl + Shift + S`) exports the focused image —
  the one in the viewer, or a single selected thumbnail — to any folder, name,
  and format. The format list offers only what your Qt build can actually
  **write**, with a quality slider for the lossy ones. With an edit pending it
  writes the edited pixels; with none it converts straight from the file on
  disk. Both run through the task pipeline, so they inherit the side-by-side
  conflict prompt.
- **Unsaved-changes guard** — leaving an edited image via prev / next or by
  closing the viewer prompts to Save, Discard, or Cancel.

### File operations

- **Side-by-side conflict previews** — the Copy / Move conflict prompt now shows
  the two clashing files next to each other (Existing vs Incoming), each with a
  thumbnail and dimensions / size / date, so you pick Skip / Rename / Overwrite
  by eye rather than by path.
- **Thumbnail rekey on move** — moves and renames keep the cached thumbnail; the
  database row is repointed instead of the thumbnail being regenerated. (Copy
  still regenerates: the source row has to stay and the destination needs its
  own.)
- **Batch rename** (`Tools → Batch rename…`) — renames the whole selection at
  once: find & replace, a `{name}` / `{n}` pattern (prefix, suffix and
  zero-padded sequential numbering all fall out of it, with a configurable Start
  / Step), and keep-extension. A live before→after table previews every name and
  flags clashes — two files heading for the same name (blocked) versus a name
  that already exists on disk (allowed; you're prompted per file). Runs through
  the task pipeline for progress and conflict handling.

### Viewer

- Added **125%** and **150%** zoom stops.

### Formats

- **`.jfif` is recognised as JPEG.** Qt's JPEG plugin advertises only
  `jpg`/`jpeg`, but a `.jfif` file is byte-identical to a `.jpg` — some Windows
  download paths and "Save for Web" exporters just pick the other suffix. The
  alias only surfaces when the underlying decoder is actually installed.

### Documentation

- Build instructions for **Debian and derivatives** (Debian 13 Trixie, Ubuntu
  24.04+, Linux Mint 22+), including how to get **WebP** via
  `qt6-image-formats-plugins` and how to build the Qt 6 **AVIF** plugin from
  source (the packaged `qt5-avif-image-plugin` / `kimageformat-plugins` are Qt 5
  and will not load).

### Internal

- All C++ sources moved under `src/`. Build files, assets, and the `tests/`,
  `themes/`, `docs/`, `installer/` and `scripts/` trees stay at the repo root.
- `.gitattributes` pins `*.bat` / `*.cmd` / `*.iss` to CRLF so Windows scripts
  survive a checkout with `core.autocrlf=input`.
- `make-portable.bat` versions its zip (`Pixee-<version>-portable.zip`) and
  auto-enables the Exiv2 backend when `thirdparty\exiv2\` is present.
- Dark-theme styling for the metadata panel and table headers.

## [0.1.0] — 2026-07-31

First versioned build — a pre-release snapshot shared with friends. Everything
below is the feature set as of this tag.

### Browsing

- **Folder browsing** with a multi-drive root and async directory enumeration
  off the GUI thread. `..` always sorts first. On Linux and macOS (single drive)
  the `/` row is hidden — the tree starts at the contents of `/` and Pixee opens
  in your home folder unless a previous session or a command-line image says
  otherwise; other volumes (`/media`, `/mnt`, `/Volumes`) stay reachable by
  browsing.
- **Background re-scan on entry** — entering a folder diffs it against disk in
  the background, so files added or removed by another app appear without a
  manual `F5`.
- **SMB-friendly** — chunked file reads with cooperative abort, off-GUI
  directory enumeration, and no `QFileSystemModel` / `QFileDialog` for the
  browser (both behave poorly on Windows network shares).

### Thumbnails

- **Thumbnail pipeline** — local SQLite cache (WAL), a four-worker decode pool,
  a viewport-driven priority queue (top-left → bottom-right), per-session
  negative caching, and automatic background fill of the rest of the folder once
  the visible cells are done.
- **Refresh thumbnail** context-menu action (for images, and for folders — which
  rebuild their index-image overlay) force-rebuilds a thumbnail from the file's
  current bytes — the escape hatch for one cached while the file was still being
  written (a long export).
- **Folder index thumbnails** — the alphabetically-first image inside each folder
  is auto-picked and overlaid on the folder icon (configurable margin / border /
  vertical offset). The pick uses the same locale-aware, case-insensitive
  ordering as the file list, so the overlay always matches the first cell you see
  on entering the folder — and it stays alphabetical no matter how the list is
  sorted.

### Sorting

- **`View → Sort by`** orders the file list by name, creation date, or
  modification date, ascending or descending (two independent, sticky choices).
  Folders stay grouped ahead of files and always sorted alphabetically A→Z (only
  the files follow the chosen key/direction); `..` stays first regardless. The
  folder tree and the folder index overlay are always name-sorted.

### Viewer

- **Integrated image viewer** — async chunked loading with a cached-thumbnail
  placeholder, fit / 1:1 / discrete zoom (`0.1×`–`8×`), pan with `Space + LMB` or
  middle-drag, 90° rotate, `F11` fullscreen, and a 5-image preload cache for
  instant prev/next.
- **Pixel-art aware** — nearest-neighbor upscaling for images smaller than the
  cell, smooth scaling for downscaling. Transparent images render over a
  configurable checker pattern.

### Formats

- **Format support** for everything Qt's image plugins can decode — JPEG, PNG,
  WebP, GIF, BMP, ICO — plus whatever extra plugins (HEIC, AVIF, PSD via
  `kimageformats`, …) are installed against your Qt build. ICO files pick the
  highest-area, highest-bit-depth sub-image.

### File operations

- **Copy / Cut / Paste / Move / Rename / Delete / New folder**, batched through a
  task pipeline with a modal conflict prompt (Skip / Rename / Overwrite). When a
  batch finishes, the files it added to the current folder are selected and
  scrolled into view (toggle in Settings).

### Settings & languages

- **Settings window** (`Edit → Settings…`) — a non-modal, always-on-top panel
  with searchable settings (type to filter any label), grouped in an icon
  sidebar, Save / Cancel. Currently: *Select added files* and *Language*.
- **Languages** — English plus Hungarian / German / French / Spanish scaffolding;
  pick one in Settings (restart to apply) or follow the OS locale. Untranslated
  strings fall back to English.

### Appearance

- **Themable** — Qt stylesheet (`style.qss`) plus an INI for non-CSS values
  (`style.ini`). User overrides drop in at `~/.pixee/themes/<name>/`. Dark theme
  included.

### Packaging (Windows)

- **Portable build** — `scripts\make-portable.bat` produces a self-contained
  folder + zip (HEIC/AVIF when built from an x64 Native Tools Command Prompt).
- **Installer** — `scripts\build-installer.bat` wraps the portable into
  `dist\Pixee-<ver>-setup.exe` via Inno Setup 6, registering Pixee in Explorer's
  **Open with…** for the bundled image types. Currently **unsigned** (SmartScreen
  warns on first run; Azure code-signing is documented for later).
- **Single-source versioning** — the version lives in one place (`VERSION.txt`)
  and flows to the app (`--version`, Help → About), the `.exe` file-properties
  resource, and the installer.

[0.1.0]: https://github.com/DynartInteractive/Pixee/releases/tag/v0.1.0
