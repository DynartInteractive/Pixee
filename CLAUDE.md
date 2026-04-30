# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Pixee is a Qt 6 / C++17 image-browser desktop app (qmake project, `Pixee.pro`). It is the in-development successor to the older `Pixie` project; Pixee is being rebuilt to deliberately avoid Qt's built-in file views/models (`QFileSystemModel` / `QFileDialog`), which behave badly on Windows when browsing internal/network shared drives. All file traversal is therefore done with a hand-written `QAbstractItemModel`.

## Build / Run

Standard qmake workflow:

```sh
qmake Pixee.pro
make            # or `nmake` / `jom` on Windows, `mingw32-make` for MinGW
./Pixee         # binary in the build dir
```

There are no tests, no linter, and no CI configured.

## Architecture

`main.cpp` constructs a single `Pixee` facade (`Pixee.cpp`), which owns the `QApplication`, `Config`, `Theme`, and `MainWindow`. `MainWindow::create()` is where all the wiring lives.

### Model layer (custom, not `QFileSystemModel`)

- **`FileItem`** — plain tree node holding a `QFileInfo`, a `FileType` enum (`Loading`, `Folder`, `File`, `Image`), a cached `QPixmap*`, and a list of children. Owns its children (`qDeleteAll` in dtor).
- **`FileModel`** — `QAbstractItemModel` implementation backed by `FileItem`. The root is hard-coded to `/home/gopher/Pictures` in the constructor (a known TODO; needs to be configurable, especially for Windows). `appendFileItems(dirPath, parent)` populates a folder's children lazily and inserts a placeholder `FileType::Loading` item under each new folder so the tree shows an expand arrow before its contents are read. The lazy-load guard (`if childCount==1 && child(0)==Loading`) means a folder's contents are only ever read once per session — re-expanding does not refresh.
- **`FileFilterModel`** — `QSortFilterProxyModel` configured per-view via `setAcceptedFileTypes(...)` and `setShowDotDot(bool)`. Sorts folders before files, then alphabetically (locale-aware, case-insensitive). `MainWindow` instantiates two of these over the same `FileModel`: one for the folder tree (`Folder` only, no `..`), one for the central list (`Folder` + `Image`, with `..`).

### View layer

- **`FolderTreeView`** (`QTreeView`) — left dock; shows the folder hierarchy via `_folderFilterModel`.
- **`FileListView`** (`QListView`) — central widget; icon-mode grid driven by `_fileFilterModel` with a custom `FileListViewDelegate` that paints the thumbnail + label per cell. Delegate pulls colors/sizing from `Theme` and `Config`.

### Navigation flow (`MainWindow.cpp`)

Three signals drive everything:
1. Folder-tree `expanded` → call `FileModel::appendFileItems` to lazy-load children of that folder.
2. Folder-tree `selectionChanged` → `goToFolderByFileIndex` repoints the central list's `rootIndex`.
3. List-view `doubleClicked` on a folder → same `goToFolderByFileIndex`. The `..` item routes to `parent->parent()`.

### Config & Theme

- **`Config`** — singleton-style holder. Reads `QImageReader::supportedImageFormats()` to build extension filters; creates `~/.pixee/` and copies a bundled `thumbnails.s3db` SQLite file there on first run. The SQLite DB is provisioned but not yet wired up (no thumbnail-cache code exists in this repo yet — that is feature work to port from Pixie).
- **`Theme`** — loads `themes/<name>/style.qss` (Qt stylesheet) + `style.ini` (color values keyed like `file-item.background-color`) + a fixed set of pixmaps/icons. Lookup order for theme assets: user folder (`~/.pixee/themes/<name>/`) first, then app folder. The `default` theme bypasses loading entirely. Pixmaps are pre-scaled to `Config::thumbnailSize() - 8` at load time.

### Persistence

`MainWindow` uses `QSettings` (org `Dynart`, app `Pixee`) to persist `mainWindowGeometry` and `mainWindowState` across sessions.

## Known rough edges (intentional, not bugs to "fix" silently)

- Hard-coded picture root in `FileModel.cpp:14`.
- Lazy-load never refreshes (commented-out reload block in `FileModel::appendFileItems`).
- Thumbnail cache (`thumbnails.s3db`) is provisioned but unused.
- `FileListViewDelegate` has commented-out border / dot-dot rendering paths kept as scaffolding.
- No translations actually shipped (`Pixee_en_US.ts` is empty).

## Qt gotchas hit in this codebase

- **`QListView::IconMode` ignores `setVerticalScrollMode(ScrollPerItem)` for the mouse wheel.** The wheel handler internally uses a small fixed pixel step regardless of the scroll mode. The setting *does* affect the scrollbar arrows and keyboard navigation, just not the wheel. Confirmed unfixed in Qt 6.x. The workaround is `FileListView::wheelEvent` overriding the wheel handler to call `verticalScrollBar()->setValue()` directly with one row per 120-unit `angleDelta()` notch (touchpad `pixelDelta()` is passed through unchanged so smooth scrolling still works). Don't suggest `setVerticalScrollMode` / `singleStep` tweaks for this — they don't take effect.
- **`QString::replace(QString, QString)` mutates the receiver in place** *and* returns a reference to it. Code like `QString out = in.replace(":/", basePath); return info.exists() ? out : in;` is buggy because `in` and `out` end up pointing at the same modified string, killing the intended fallback. Use a local copy first: `QString out = in; out.replace(":/", basePath);`. (Was a real bug in `Theme::realPath` — fixed.)
