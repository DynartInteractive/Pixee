# Changelog

All notable changes to Pixee are documented here. The format is loosely based
on [Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/) (pre-1.0: features bump the minor,
fixes bump the patch).

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
