# Pixie vs Pixee — comparison

Pixie is the older project. Pixee is the in-development successor. The two live in sibling repos.

## Stack & build

| | Pixie (older) | Pixee (in development) |
|---|---|---|
| Qt | Qt 5 (`core gui sql widgets`) | Qt 6 (`core gui widgets`) |
| C++ | C++11 | C++17 |
| Extra modules | needs `QT += sql` for SQLite cache | none yet (cache not wired) |
| Build | `qmake -makefile` then `make` | same |
| Translations | none | `Pixee_en_US.ts` (empty placeholder, `lrelease`/`embed_translations` already wired) |

## File model — the core difference

- **Pixie** uses Qt's *item-widget* convenience classes: `QTreeWidget`/`QListWidget` with `QTreeWidgetItem`/`QListWidgetItem`. The widgets *are* the storage; items are also indexed in `QHash<QString, *>` keyed by path. There is no `QAbstractItemModel` at all. This is the "low-quality model" being replaced.
- **Pixee** uses Qt's proper *model/view* with a hand-rolled `QAbstractItemModel` (`FileModel` over a `FileItem` tree) plus `QSortFilterProxyModel` subclass `FileFilterModel` (one instance configured for the folder tree, another for the file list) feeding plain `QTreeView`/`QListView`. Sorting & filtering live in the proxy.
- Critically, **neither** uses `QFileSystemModel`/`QFileDialog` — that goal is preserved in Pixee. (Pixie does briefly use `QFileDialog::getExistingDirectory` in `ViewWindow::copyToTriggered` for the "Copy to…" dialog — that's the one spot that would need replacing if ported.)

## Threading & async I/O

- **Pixie**: three dedicated `QThread`s (file-manager worker, thumbnail SQLite DB, fullscreen image worker) + `QThreadPool::globalInstance()` for thumbnail generation and parallel row-by-row decode of large images. All communication is queued signals/slots; custom types (`File*`, `FoundFile`, `FoundFolder`) are `qRegisterMetaType`-registered.
- **Pixee**: synchronous, single-threaded directory scan in `FileModel::appendFileItems` (uses `QDir::entryInfoList` directly on the GUI thread). On Windows network shares this *will* hitch — porting Pixie's `FileManagerWorker` pattern is one of the obvious next steps.

## Features in Pixie that Pixee does not yet have

1. **Multi-drive support** — Pixie calls `QDir::drives()` and synthesizes a root with one child per drive letter. Pixee hard-codes `/home/gopher/Pictures` (`FileModel.cpp:14`) — broken on Windows.
2. **Fullscreen image viewer** (`ViewWindow` + `ViewWidget`) — pan, zoom, rotate, fit-to-window, F11 fullscreen, ←/→/wheel navigation, "Copy to…" with last-path memory, EXIF orientation. None of this exists in Pixee.
3. **Progressive large-image loading** — `ImageWorker` + `ImageRowWorker` decode large images row-by-row in parallel using `QImageReader::ClipRect`, painting tiles in as they arrive.
4. **SQLite thumbnail cache** — `ThumbnailDatabase` + `ThumbnailQueue` + `ThumbnailWorker`. Pixee already provisions `~/.pixee/thumbnails.s3db` from a bundled resource but **nothing reads or writes it yet** — the plumbing is missing.
5. **Folder thumbnails** — Pixie's `ThumbnailWorker` walks into a folder and uses the first image as the folder thumbnail.
6. **Restore last folder on startup** + **CLI start path** (open with a file/folder argument, launches straight into viewer).
7. **Menu bar** (File/Tools/Help, Quit, Settings stub, About).
8. **Status bar** with image/folder counts.
9. **Clipboard paste** — Ctrl+V copies image files into the current folder (`pasteFiles()`).
10. **Path edit field** updates as a window title with `\` vs `/` based on platform (`Config::useBackslash`).
11. **Settings persistence** — Pixie persists `lastFolderPath`, `viewWindowGeometry`/`State`, `lastCopyToPath`. Pixee persists only `mainWindowGeometry`/`State`.

## Features Pixee has that Pixie does not

- A `Theme` that also caches a fixed set of pre-scaled `QPixmap*` and `QIcon*` (Pixie's theme only does stylesheet + ini values).
- `..` (dot-dot) navigation modeled directly into the file tree (Pixie has it via a synthetic `dotDotFile` instance reused everywhere).
- Cleaner ownership: Pixee groups everything under a `Pixee` facade that's passed in instead of via getters.

## Recommended next steps for Pixee, in priority order

1. Replace the hard-coded `/home/gopher/Pictures` root with a multi-drive root via `QDir::drives()` (port `FileManager::findDrives` and the synthetic root pattern).
2. Move directory enumeration off the GUI thread — port `FileManagerWorker` essentially as-is. The signal/slot shape from Pixie translates 1-to-1 onto `FileModel`'s `beginInsertRows`/`endInsertRows`.
3. Wire the existing `~/.pixee/thumbnails.s3db` to a `ThumbnailQueue` analogue and have `FileListViewDelegate` pull cached pixmaps instead of the static placeholder.
4. Lazy-load **refresh** in `FileModel::appendFileItems` (the commented-out reload block) — currently a folder is only ever read once per session.
5. Build a `ViewWindow`/`ViewWidget` analogue for fullscreen viewing — this is probably the biggest single feature gap.
