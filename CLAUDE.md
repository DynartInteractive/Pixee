# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Pixee is a Qt 6 / C++17 image-browser desktop app (qmake project, `Pixee.pro`). It is the successor to the older `Pixie`; the rebuild deliberately avoids Qt's built-in file views/models (`QFileSystemModel` / `QFileDialog`), which behave badly on Windows when browsing internal/network shared drives. All file traversal is therefore done with a hand-written `QAbstractItemModel`.

The `README.md` is the authoritative feature/keyboard list — when adding or changing user-visible behavior, update it.

## Build / Run

Qt 6.6+ via qmake:

```sh
qmake Pixee.pro
make            # nmake / jom on Windows MSVC; mingw32-make for MinGW
./Pixee         # binary in the build dir
```

The `.pro` file copies the `themes/` tree next to the binary on every build (see the `copy_themes` extra-target near the bottom). No lint or CI is configured.

### Tests

Test suites live under `tests/` as a qmake `subdirs` project — one binary per subdirectory:

```sh
cd tests
qmake tests.pro && make
./CopyFileTask/tst_CopyFileTask   # or any other tst_*
```

Suites are split per binary on purpose so a crash in one doesn't take the others down. To add a suite: create `tests/<Name>/tst_<Name>.cpp` + `.pro`, then list `<Name>` in `tests/tests.pro` `SUBDIRS`. Each `.pro` pulls source files from `..` directly (no library build); copy an existing `.pro` as a template. Shared fixtures are `tests/TaskTestFixture.{h,cpp}` and `tests/TestHelpers.{h,cpp}`.

There are tests for the task layer (Copy/Move/Delete/FolderCleanup/Group/Image tasks) and folder expansion. There are no UI tests.

## Architecture

`main.cpp` constructs a single `Pixee` facade (`Pixee.cpp`), which owns the `QApplication`, `Config`, `Theme`, `ThumbnailCache`, `TaskManager`, and `MainWindow`. `MainWindow::create()` is where all the wiring lives.

### Threading model

Cross-thread communication is **always** via Qt signals/slots with queued connections — no shared state, no manual mutexes outside the task / thumbnail-cache plumbing. Long-lived worker threads:

- **Thumbnail DB thread** (`ThumbnailCache::_dbThread`) — owns `ThumbnailDatabase`, the SQLite (WAL) connection at `~/.pixee/thumbnails.s3db`.
- **Thumbnail decode pool** — `ThumbnailGenerator` dispatches to N `ThumbnailWorker`s (one thread each, `Config::maxThreadCount()` = 4) ordered by a min-priority queue. Lower priority number runs first; `ThumbnailCache::setPriority` re-ranks based on viewport distance (top-left first).
- **Folder enumeration thread** (`FileModel::_enumThread`) — `FolderEnumerator`, initial-load directory reads.
- **Folder refresh thread** (`FileModel::_refreshThread`) — `FolderRefresher`, separate from enumeration so a slow refresh doesn't queue behind an initial load.
- **Viewer image-loader thread** (`MainWindow::_imageLoaderThread`) — `ImageLoader`, chunked full-resolution decode for the viewer with cooperative abort via a version counter.
- **Task worker pool** (`TaskManager`) — N `TaskRunner`s (one thread each, `Config::taskWorkerCount()` = 2) for Copy/Move/Delete/FolderCleanup/Scale/Convert.

Abort/supersede pattern used in `ThumbnailGenerator`, `ImageLoader`, and `FileModel::_refreshVersions`: a `QAtomicInt` version is bumped when a snapshot is invalidated; workers compare their dispatch-time snapshot against the live counter and bail with an `aborted` signal if they differ.

### Model layer (custom, not `QFileSystemModel`)

- **`FileItem`** — plain tree node holding a `QFileInfo`, a `FileType` enum (`Loading`, `Folder`, `File`, `Image`), a cached `QPixmap*`, and a list of children. Owns its children (`qDeleteAll` in dtor).
- **`FileModel`** — `QAbstractItemModel` over `FileItem`. The synthetic root holds one child per drive (`QDir::drives()`) — drives get a `hard-drive` icon and show their drive-letter path. Each new folder gets a `FileType::Loading` placeholder child so the expand arrow appears before contents are read. Custom roles: `ThumbnailRole`, `ThumbnailStateRole`, `IndexImageRole`, `IndexSourcePathRole`. Per-folder "index thumbnail" (`_folderIndexes` / `_indexUsers`) automatically picks the alphabetically-first image inside a folder and overlays it on the folder icon. Path → item reverse lookup via `_itemsByPath`. **Refreshes are real now** — `requestRefreshFolder` diffs disk against the live snapshot via `FolderRefresher` and applies adds/removes/modifies surgically (so existing thumbnails survive). `renameItem` and `createFolder` perform the on-disk operation and rekey caches in place.
- **`FileFilterModel`** — `QSortFilterProxyModel` configured per-view via `setAcceptedFileTypes(...)` and `setShowDotDot(bool)`. Sorts folders before files, then alphabetically (locale-aware, case-insensitive). `MainWindow` instantiates two over the same `FileModel`: one for the folder tree (`Folder` only, no `..`), one for the central list (`Folder` + `Image` + `File`, with `..`).

### Thumbnail pipeline

`ThumbnailCache` is the GUI-thread facade. Flow per `subscribe(path, mtime, size, distance)`:
1. Skipped if path is in the per-session negative cache (`_failures`).
2. DB lookup (`requestLookup` → `ThumbnailDatabase`). Path-keyed; mtime + size validate freshness. Storage format: PNG when source has alpha, JPEG otherwise; format auto-detected on read.
3. On miss: hand to `ThumbnailGenerator::enqueue` with the current priority. Workers decode + downscale + JPEG/PNG-encode + emit `generated`.
4. Cache `requestSave`s the encoded bytes and emits `thumbnailReady(path, image)`.

`abandonAll()` is the fast path used on folder change: drops every subscription and bumps the generator's abort version so in-flight decodes bail.

`FileListView` is the heaviest subscriber — it computes a viewport-driven prefetch window (visible rows + a margin), subscribes/unsubscribes diff-only on a debounced timer (`_updateTimer`), bumps priorities by viewport distance, and auto-expands the window once the current batch finishes so background fill of the rest of the folder happens after the visible cells are ready.

### View layer

- **`FolderTreeView`** (`QTreeView`) — left dock; shows the folder hierarchy via `_folderFilterModel`. Accepts external drops onto folders (routed through `TaskManager`).
- **`FileListView`** (`QListView`) — central widget; icon-mode grid driven by `_fileFilterModel` with a custom `FileListViewDelegate` that paints the thumbnail + label per cell. Handles thumbnail-cache subscriptions, drag-out (`startDrag`), and drag-in (`drop*Event`).
- **`ViewerWidget`** — image viewer in the central `QStackedWidget`. Phase 3+: fit / 1:1 / discrete zoom (`kZoomLevels` 0.1×–8×), pan with `Space+LMB` or `Middle-drag`, 90° rotate, fullscreen, lockable view state (zoom/pan/fit survive prev/next when `lockZoom()` is on; rotation always resets per-image). Cached thumbnail is shown as a placeholder while the full-res load is in flight.

### Navigation & viewer flow (`MainWindow.cpp`)

Folder navigation:
1. Folder-tree `expanded` → `FileModel::requestEnumerate` to lazy-load children off-thread.
2. Folder-tree `selectionChanged` / list-view `doubleClicked` on a folder → `goToFolderByFileIndex` repoints the central list's `rootIndex`.
3. `..` routes to the parent of the parent.
4. `F5` triggers `refreshCurrentFolder`. After any task completes, `TaskManager::pathTouched` debounces affected directories through `_touchedDirsTimer` and refreshes them; folders touched while the user is elsewhere are tracked in `_staleDirs` and refreshed lazily on revisit.

Viewer flow: double-click an image (or `Enter`) → `activateImage` swaps the central stack to `ViewerWidget`, builds an image-path list from the current folder, starts an async `ImageLoader` request, and preloads 5-image neighbours into `_viewerImageCache`. Thumbnails act as placeholders until the full-res arrives. Esc / Enter / double-click dismisses; the folder-tree dock visibility is remembered from before the viewer was opened so dismissing doesn't unhide a dock the user had closed.

### Task system

A unified pipeline for Copy / Move / Delete / FolderCleanup / Scale / ConvertFormat:

- **`Task`** (abstract) — single-file operation. Subclasses override `run()` and must call `checkPauseStop()` at chunk boundaries to stay responsive. Conflict handling via `resolveOrAsk(QuestionKind, context)` — blocks on a per-task `QWaitCondition` until the GUI thread answers, or consults the group's sticky answer first.
- **`TaskGroup`** — owns a set of related tasks (e.g. one Copy group containing per-file `CopyFileTask`s). Holds the sticky-answer map ("Skip All" / "Overwrite All" propagate within a group).
- **`TaskRunner`** — single-thread worker that pulls tasks one at a time.
- **`TaskManager`** — GUI-thread facade. Owns the pool (`Config::taskWorkerCount()` = 2). Dispatch rule: **within a group, tasks run sequentially; across groups, runners parallelise.** This is intentional — at most one task per group can be asking a conflict question at any moment, so "Skip All" always wins the next conflict in the same group without races. `pathTouched` is emitted per affected directory once a task completes; `MainWindow` debounces and refreshes.
- **Dock + status bar** — `TaskDockWidget` shows live groups/tasks (`TaskGroupWidget`, `TaskItemWidget`); `TaskStatusWidget` is the bottom-of-window summary that toggles dock visibility on click. Dock auto-hides when idle; the user's explicit show/hide is sticky across runs (`QSettings`).
- **`FileOpsMenuBuilder`** — builds the shared Copy/Move/Delete/Paste/Rename/NewFolder context menu used by both the file list and the viewer. Also exposes static helpers for Ctrl+C/V handlers, drag-out (`buildPathsMimeData`), and drop handlers (`handleDropOrPaste`). Recent destination folders persist as `lastCopyToPath` / `lastMoveToPath` in `QSettings` and are shared between the two menus.

### Config & Theme

- **`Config`** — Reads `QImageReader::supportedImageFormats()` to build extension filters; creates `~/.pixee/` and copies the bundled `:/database/thumbnails.s3db` SQLite file there on first run. `thumbnailSize() = 256`, `maxThreadCount() = 4` (thumbnail decoders), `taskWorkerCount() = 2`. `useBackslash()` returns false on Linux, true elsewhere.
- **`Theme`** — loads `themes/<name>/style.qss` (Qt stylesheet) + `style.ini` (color/int values keyed like `file-item.background-color`) + a fixed set of pixmaps/icons. Lookup order: user folder (`~/.pixee/themes/<name>/`) first, then app folder. `default` bypasses loading entirely. Pixmaps are pre-scaled to `Config::thumbnailSize() - 8` at load time.

### Persistence

`MainWindow` uses `QSettings` (org `Dynart`, app `Pixee`) for `mainWindowGeometry`, `mainWindowState`, the user's tasks-dock visibility intent, and recent Copy/Move destinations. The SQLite thumbnail DB sits at `~/.pixee/thumbnails.s3db`.

## Qt gotchas hit in this codebase

- **`QListView::IconMode` ignores `setVerticalScrollMode(ScrollPerItem)` for the mouse wheel.** The wheel handler internally uses a small fixed pixel step regardless of the scroll mode. The setting *does* affect the scrollbar arrows and keyboard navigation, just not the wheel. Confirmed unfixed in Qt 6.x. The workaround is `FileListView::wheelEvent` overriding the wheel handler to call `verticalScrollBar()->setValue()` directly with one row per 120-unit `angleDelta()` notch (touchpad `pixelDelta()` is passed through unchanged so smooth scrolling still works). Don't suggest `setVerticalScrollMode` / `singleStep` tweaks for this — they don't take effect.
- **`QString::replace(QString, QString)` mutates the receiver in place** *and* returns a reference to it. Code like `QString out = in.replace(":/", basePath); return info.exists() ? out : in;` is buggy because `in` and `out` end up pointing at the same modified string, killing the intended fallback. Use a local copy first: `QString out = in; out.replace(":/", basePath);`. (Was a real bug in `Theme::realPath` — fixed.)
- **`dataChanged` alone does not make views re-fetch via `Qt::DecorationRole` or custom roles for an existing row** — only `rowsInserted` / `layoutChanged` / `modelReset` re-trigger thumbnail subscription. `FileModel::thumbnailInvalidated` exists for the in-place-modify case so `FileListView::onThumbnailInvalidated` can drop and re-subscribe.
- **Tasks that touch a folder must list it in `affectedDirs()`** so `TaskManager::pathTouched` fires and the UI refreshes. Forgetting this leaves stale rows in the model after a successful operation.
