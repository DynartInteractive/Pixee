# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Pixee is a Qt 6 / C++17 image-browser desktop app (qmake project, `Pixee.pro`). It is the successor to the older `Pixie`; the rebuild deliberately avoids Qt's built-in file views/models (`QFileSystemModel` / `QFileDialog`), which behave badly on Windows when browsing internal/network shared drives. All file traversal is therefore done with a hand-written `QAbstractItemModel`.

The `README.md` is the authoritative feature/keyboard list — when adding or changing user-visible behavior, update it.

### Target display & UI density

**Pixee must run without issues at 1280×720** — that's the baseline resolution the UI is designed for (a small laptop / a modest window, not a maximised 4K desktop). Treat it as a hard constraint, not an aspiration: every window, dialog, dock, and menu has to be fully usable at that size.

The main practical consequence is **UI density** — especially **context (right-click) menus**. A right-click menu must fit comfortably within 720px tall without scrolling or running off-screen (Qt will scroll an over-tall menu, which is a poor experience). So when adding actions to the shared `FileOpsMenuBuilder` / viewer / list menus, **count the rows**: if a flat menu starts pushing ~15+ items, restructure — group related actions into submenus (e.g. a "Rotate ▸", "Copy to ▸" pattern), promote the common few to the top level and tuck the rest under a submenu, or split by separators. The same applies to settings pages, toolbars, and any list that grows unbounded. When a change would make a menu or panel exceed the 1280×720 budget, prefer restructuring over just letting it grow.

**Source layout:** all C++ (`*.cpp` / `*.h`) lives under `src/`. Build files and assets stay at the repo root: `Pixee.pro`, `resources.qrc`, and `VERSION.txt`, plus the `translations/` (the `Pixee_*.ts` catalogues), `resources/`, `themes/`, `docs/`, `installer/`, `scripts/`, and `tests/` trees. Includes are flat (`#include "Config.h"`, no path prefix) — they resolve within `src/`, so adding a source file just means listing it in `Pixee.pro`'s `SOURCES`/`HEADERS` with the `src/` prefix.

### Versioning

The version is **single-sourced from the repo-root `VERSION.txt`** (SemVer; pre-1.0 while unreleased, so features bump minor / fixes bump patch). To release: edit that one line, build, `git tag v<version>`. It flows to: `Pixee.pro` (`VERSION = $$cat($$PWD/VERSION.txt)` → the `.exe` file-properties resource, plus `DEFINES += APP_VERSION=...`), the code (`APP_VERSION`, with a `"0.0.0-dev"` fallback — Help→About and `main.cpp`'s `--version`/`-v`), and the installer (`build-installer.bat` passes `/DAppVersion` to ISCC; a direct `iscc` reads `VERSION.txt` via `SourcePath`). The `.txt` extension is load-bearing: keep it. An extensionless `VERSION` file shadows the C++ `<version>` header on case-insensitive Windows whenever its directory lands on the compiler include path, breaking the build — this bit us when the sources sat at the repo root, and would again if a `VERSION` file ever appeared in `src/`. The `.txt` suffix sidesteps it everywhere.

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

Suites are split per binary on purpose so a crash in one doesn't take the others down. To add a suite: create `tests/<Name>/tst_<Name>.cpp` + `.pro`, then list `<Name>` in `tests/tests.pro` `SUBDIRS`. Each `.pro` pulls app source files from `../../src/` directly (no library build) and shared fixtures from `../`; copy an existing `.pro` as a template. Shared fixtures are `tests/TaskTestFixture.{h,cpp}` and `tests/TestHelpers.{h,cpp}`.

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
- **Metadata reader thread** (`MainWindow::_metadataThread`) — `MetadataReader`, off-thread read for the info panel with the same version-counter supersede as `ImageLoader`. Emits a plain `ImageMetadata` struct (registered metatype); Exiv2 stays inside `MetadataReader.cpp`.
- **Task worker pool** (`TaskManager`) — N `TaskRunner`s (one thread each, `Config::taskWorkerCount()` = 2) for Copy/Move/Delete/FolderCleanup/Scale/Convert.

Abort/supersede pattern used in `ThumbnailGenerator`, `ImageLoader`, and `FileModel::_refreshVersions`: a `QAtomicInt` version is bumped when a snapshot is invalidated; workers compare their dispatch-time snapshot against the live counter and bail with an `aborted` signal if they differ.

### Model layer (custom, not `QFileSystemModel`)

- **`FileItem`** — plain tree node holding a `QFileInfo`, a `FileType` enum (`Loading`, `Folder`, `File`, `Image`), a cached `QPixmap*`, and a list of children. Owns its children (`qDeleteAll` in dtor).
- **`FileModel`** — `QAbstractItemModel` over `FileItem`. The synthetic root holds one child per drive (`QDir::drives()`) — drives get a `hard-drive` icon and show their drive-letter path. On platforms where `Config::hasDriveList()` is false (Linux and macOS, which only ever have `/`) `MainWindow` sets the folder tree's `rootIndex` to the `/` item so its children are the top level, and `navigateTo` redirects anything aimed at the synthetic root to `/` instead — the drive list is never shown. This is view-level only; the model still holds the `/` item, so `..` and path lookups are unchanged. Each new folder gets a `FileType::Loading` placeholder child so the expand arrow appears before contents are read. Custom roles: `ThumbnailRole`, `ThumbnailStateRole`, `IndexImageRole`, `IndexSourcePathRole`. Per-folder "index thumbnail" (`_folderIndexes` / `_indexUsers`) automatically picks the alphabetically-first image inside a folder and overlays it on the folder icon. The pick goes through `repickFolderIndex` (used by both the initial `applyEntries` and the refresh diff) which uses the **shared `FileModel::nameLessThan`** — the same ordering `FileFilterModel` uses for name-sorting — so the overlay always matches the list view's first cell. It is deliberately **always alphabetical**, independent of the user's chosen list `SortKey`. Path → item reverse lookup via `_itemsByPath`. **Refreshes are real now** — `requestRefreshFolder` diffs disk against the live snapshot via `FolderRefresher` and applies adds/removes/modifies surgically (so existing thumbnails survive). `renameItem` and `createFolder` perform the on-disk operation and rekey caches in place.
- **`FileFilterModel`** — `QSortFilterProxyModel` configured per-view via `setAcceptedFileTypes(...)` and `setShowDotDot(bool)`. Ordering invariants (never inverted): `..` first, then folders before files, and **folders always sort alphabetically A→Z** — the `SortKey`/direction apply to files only, so the folder block is a stable index at the top. Within the file group it sorts by a **`SortKey`** (`Name` / `Created` / `Modified`) and a direction (`setSortAscending`). `Name` uses `FileModel::nameLessThan` (locale-aware, case-insensitive, case-sensitive tiebreak); the date keys compare `QFileInfo::birthTime()` / `lastModified()` with a name tiebreak (invalid times — e.g. no `birthTime` on a share — sort oldest). The proxy stays in `AscendingOrder`; **direction is applied inside `lessThan`** so flipping to Descending doesn't shove `..`/folders to the bottom. `setSortKey`/`setSortAscending` call `invalidate()` to re-sort. `MainWindow` instantiates two over the same `FileModel`: the folder tree (`Folder` only, no `..`) is left name-ascending; the central list (`Folder` + `Image` + `File`, with `..`) restores the user's `View → Sort by` choice from `QSettings` (`view/sortBy`, `view/sortOrder` in `AppSettings.h`).

### Thumbnail pipeline

`ThumbnailCache` is the GUI-thread facade. Flow per `subscribe(path, mtime, size, distance)`:
1. Skipped if path is in the per-session negative cache (`_failures`).
2. DB lookup (`requestLookup` → `ThumbnailDatabase`). Path-keyed; mtime + size validate freshness. Storage format: PNG when source has alpha, JPEG otherwise; format auto-detected on read.
3. On miss: hand to `ThumbnailGenerator::enqueue` with the current priority. Workers decode + downscale + JPEG/PNG-encode + emit `generated`.
4. Cache `requestSave`s the encoded bytes and emits `thumbnailReady(path, image)`.

`abandonAll()` is the fast path used on folder change: drops every subscription and bumps the generator's abort version so in-flight decodes bail.

`refreshThumbnail(path, mtime, size)` is the manual override behind the "Refresh thumbnail" menu item (`FileModel::refreshThumbnail` → here): it clears the `_failures` entry and force-enqueues generation **bypassing step 1–2**, so a thumbnail cached from a still-being-written file (a long export) — which the negative cache would otherwise never retry this session — can be rebuilt from the file's current bytes. `FileModel::refreshThumbnail` dispatches on type: a folder path routes to `refreshFolderThumbnail`, which refreshes the folder's *index-image* thumbnail (the folder cell repaints via the `_indexUsers` fan-out).

`moveThumbnail(oldPath, newPath)` repoints a cached thumbnail after a rename/move instead of regenerating it: a rename/move keeps the file's bytes, so only the path key changed. It stats `newPath` for its current mtime/size and hands the rekey to the DB thread (`ThumbnailDatabase::rekey` — one `UPDATE OR REPLACE pixee_thumbnails SET path,mtime,size WHERE path=old`; `UPDATE OR REPLACE` drops any row already at the new path, i.e. the overwrite case), and clears the old path's `_failures` entry. Driven by `FileModel::pathRenamed` (single-file rename) and `TaskManager::pathMoved` (Move/Rename tasks), both wired in `MainWindow`. **Copy is deliberately not covered** — the source row must stay and the destination needs its own row (a BLOB duplicate, not a rekey), so a copy regenerates.

`FileListView` is the heaviest subscriber — it computes a viewport-driven prefetch window (visible rows + a margin), subscribes/unsubscribes diff-only on a debounced timer (`_updateTimer`), bumps priorities by viewport distance, and auto-expands the window once the current batch finishes so background fill of the rest of the folder happens after the visible cells are ready.

### View layer

- **`FolderTreeView`** (`QTreeView`) — left dock; shows the folder hierarchy via `_folderFilterModel`. Accepts external drops onto folders (routed through `TaskManager`).
- **`FileListView`** (`QListView`) — central widget; icon-mode grid driven by `_fileFilterModel` with a custom `FileListViewDelegate` that paints the thumbnail + label per cell. Handles thumbnail-cache subscriptions, drag-out (`startDrag`), and drag-in (`drop*Event`).
- **`ViewerWidget`** — image viewer in the central `QStackedWidget`. Phase 3+: fit / 1:1 / discrete zoom (`kZoomLevels` 0.1×–8×), pan with `Space+LMB` or `Middle-drag`, 90° rotate, fullscreen, lockable view state (zoom/pan/fit survive prev/next when `lockZoom()` is on; rotation always resets per-image). Cached thumbnail is shown as a placeholder while the full-res load is in flight.
- **`MetadataPanel`** — read-only info dock (`View → Metadata`, right side, hidden by default, sticky in `QSettings` `metadataDockEnabled`). Renders the `ImageMetadata` from the off-thread `MetadataReader` in a grouped two-column `QTreeWidget`. Driven for the **focused image**: the viewer's current image while the viewer is up, otherwise the file list's current row (via `currentChanged`, debounced ~120 ms). Reads are **gated on the dock being visible** (no disk I/O when hidden) and superseded by an abort-version counter. Pure Qt — no Exiv2 dependency; the EXIF/IPTC/XMP fields are populated only in a `PIXEE_HAVE_EXIV2` build (see `docs/metadata.md`), otherwise it shows Qt-only basics. **Embedded PNG text chunks** (`QImageReader::textKeys()`/`text()`) are read in *every* build — this is where AI-tool generation data lives (ComfyUI `prompt`/`workflow`, A1111 `parameters`); Exiv2 does not surface those custom keys, so `ImageMetadata::textChunks` is a separate Qt-read list shown in its own "Embedded text" group. Long values are elided in-cell (full value kept in `Qt::UserRole`); right-click / `Ctrl+C` copies the full value. Note `ImageLoader` already sets `QImageReader::setAutoTransform(true)`, so the viewer already honours EXIF orientation; the panel just reports the tag.

### Navigation & viewer flow (`MainWindow.cpp`)

Folder navigation:
1. Folder-tree `expanded` → `FileModel::requestEnumerate` to lazy-load children off-thread.
2. Folder-tree `selectionChanged` / list-view `doubleClicked` on a folder → `goToFolderByFileIndex` repoints the central list's `rootIndex`.
3. `..` routes to the parent of the parent.
4. Startup restores the saved `lastPath` (or the CLI image's folder) via the async chain descent in `beginPathRestore` / `advancePathRestore`; with no drive list and nothing saved, the fallback target is `QDir::homePath()`.
5. `F5` triggers `refreshCurrentFolder`. Every `navigateTo` also fires a background `requestRefreshFolder` on entry (cheap diff; no-op when nothing changed), so a folder re-entered after an external change — a GIMP export, another app — shows current contents without F5. After any task completes, `TaskManager::pathTouched` debounces affected directories through `_touchedDirsTimer` and refreshes the current folder if it's among them; folders touched while the user is elsewhere are picked up by the on-entry refresh (there's no separate stale-set).

Viewer flow: double-click an image (or `Enter`) → `activateImage` swaps the central stack to `ViewerWidget`, builds an image-path list from the current folder, starts an async `ImageLoader` request, and preloads 5-image neighbours into `_viewerImageCache`. Thumbnails act as placeholders until the full-res arrives. Esc / Enter / double-click dismisses; the folder-tree dock visibility is remembered from before the viewer was opened so dismissing doesn't unhide a dock the user had closed.

### Task system

A unified pipeline for Copy / Move / Rename / Delete / FolderCleanup / Scale / ConvertFormat:

- **`Task`** (abstract) — single-file operation. Subclasses override `run()` and must call `checkPauseStop()` at chunk boundaries to stay responsive. Conflict handling via `resolveOrAsk(QuestionKind, context)` — blocks on a per-task `QWaitCondition` until the GUI thread answers, or consults the group's sticky answer first.
- **`TaskGroup`** — owns a set of related tasks (e.g. one Copy group containing per-file `CopyFileTask`s). Holds the sticky-answer map ("Skip All" / "Overwrite All" propagate within a group).
- **`TaskRunner`** — single-thread worker that pulls tasks one at a time.
- **`TaskManager`** — GUI-thread facade. Owns the pool (`Config::taskWorkerCount()` = 2). Dispatch rule: **within a group, tasks run sequentially; across groups, runners parallelise.** This is intentional — at most one task per group can be asking a conflict question at any moment, so "Skip All" always wins the next conflict in the same group without races. `pathTouched` is emitted per affected directory once a task completes; `MainWindow` debounces and refreshes. On group completion it also emits `groupFinished(producedPaths)` — the destinations of the group's *completed* tasks (`Task::producedPaths()`, overridden by Copy/Move/Scale/Convert). `MainWindow::onGroupFinished` stashes the ones in the current folder and selects them once the refresh inserts their rows (gated by `fileOps/selectAddedFiles`, default on). Tasks that preserve a file's bytes (`MoveFileTask`, `RenameTask`) also override `Task::movedPaths()` (old→new pairs); `TaskManager` turns those into `pathMoved` — emitted *before* `pathTouched` — which `MainWindow` routes to `ThumbnailCache::moveThumbnail` so the cached thumbnail's DB row is repointed (see the thumbnail pipeline note on `rekey`) instead of the folder refresh regenerating it.
- **Conflict prompt** — `TaskConflictPrompter` (GUI-thread, one modal at a time via a nested-loop FIFO) turns a task's `DestinationExists` question into a `ConflictDialog`. That dialog shows the two clashing files **side by side** (Existing = dst, Incoming = src) with a thumbnail + dimensions/size/date each; thumbnails load through the shared `ThumbnailCache` and fill in live during `exec()`'s nested event loop. The prompter carries the cache pointer through from `MainWindow`.
- **Batch rename** — `Tools → Batch rename…` (`MainWindow::openBatchRename`) opens `BatchRenameDialog` over the file-list selection. Name computation and clobber-safe ordering are pure functions in **`BatchRenamePlan`** (`newNameFor` = find/replace → `{name}`/`{n}` pattern → keep-extension; `planRenameSteps` orders renames so none overwrites a file still waiting to move, tail-first for chains, and flags a true `a↔b` cycle as unresolvable). The dialog enqueues one `RenameTask` per changed file as a group; `tests/BatchRename` covers the pure logic. Keep this menu lean — see the 1280×720 density note; `Tools` is a new top-level menu.
- **Save / Save As** — `File → Save As…` (`MainWindow::saveImageAs`, `Ctrl+Shift+S`) opens `SaveAsDialog` over the **focused image** (`currentContextImagePath()` — the viewer's image, else the single selected list image) and enqueues a `ConvertFormatTask` as a one-task group, so it inherits the side-by-side conflict prompt, progress dock, and produced-file selection. This round it converts **from the file on disk** — there are no in-memory edits yet; when editing ops land the caller hands the edited image straight to the task. `SaveAsDialog` gathers folder (native `getExistingDirectory`, same picker as Copy to/Move to, remembers `fileOps/lastSaveAsPath`) + name + format + quality; the format dropdown is fed by the **new `Config::writableImageFormats()`** (`QImageWriter::supportedImageFormats()`, lowercased/deduped — the write-side counterpart to `imageExtensions()`, which is read-side and strictly larger), and the quality slider only shows for lossy formats (jpg/jpeg/webp). `File → Save` (`Ctrl+S`, `MainWindow::saveImage`) overwrites the original in place and is **dormant**: it's gated on `ViewerWidget::isModified()`, which nothing sets until the Crop/Flip/Rotate round, so it stays greyed today. Enablement for both is centralised in `updateSaveActions()` (member `_saveAction`/`_saveAsAction`), called on viewer show/dismiss, image-loaded, list selection changes, and `ViewerWidget::modifiedChanged`. The viewer's dirty flag resets to false on every image change (`setImage`/`setPlaceholder`/`clear`) — **next round's editing ops must add an unsaved-changes guard**, since prev/next currently clobbers view state and would silently drop a pending edit. The real Save write path (lossless EXIF-orientation write for a pure rotate; re-encode + confirm for a pixel edit) also lands next round and needs **Exiv2 write** — the metadata backend is read-only today.
- **Dock + status bar** — `TaskDockWidget` shows live groups/tasks (`TaskGroupWidget`, `TaskItemWidget`); `TaskStatusWidget` is the bottom-of-window summary that toggles dock visibility on click. Dock auto-hides when idle; the user's explicit show/hide is sticky across runs (`QSettings`).
- **`FileOpsMenuBuilder`** — builds the shared Copy/Move/Delete/Paste/Rename/NewFolder context menu used by both the file list and the viewer. Also exposes static helpers for Ctrl+C/V handlers, drag-out (`buildPathsMimeData`), and drop handlers (`handleDropOrPaste`). Recent destination folders persist as `lastCopyToPath` / `lastMoveToPath` in `QSettings` and are shared between the two menus.

### Config & Theme

- **`Config`** — Reads `QImageReader::supportedImageFormats()` to build the read-side extension filters, and `QImageWriter::supportedImageFormats()` for `writableImageFormats()` (the Save As dropdown source — write support is a strict subset of read); creates `~/.pixee/` and copies the bundled `:/database/thumbnails.s3db` SQLite file there on first run. `thumbnailSize() = 256`, `maxThreadCount() = 4` (thumbnail decoders), `taskWorkerCount() = 2`. `useBackslash()` returns false on Linux, true elsewhere.
- **`Theme`** — loads `themes/<name>/style.qss` (Qt stylesheet) + `style.ini` (color/int values keyed like `file-item.background-color`) + a fixed set of pixmaps/icons. Lookup order: user folder (`~/.pixee/themes/<name>/`) first, then app folder. `default` bypasses loading entirely. Pixmaps are pre-scaled to `Config::thumbnailSize() - 8` at load time.

### Settings & i18n

- **`SettingsDialog`** — a modeless, `WindowStaysOnTopHint` window (`Edit → Settings…`), single-instance (raised if already open, `WA_DeleteOnClose` + `QPointer` in `MainWindow`). Built from a declarative registry in the `.cpp` (groups → settings; `Bool`/`Choice` controls) so adding a setting is a one-liner. Left icon list (SVG, `:/icons/settings/*.svg`) + right stacked pages; a top search box filters/highlights rows by label. Values are staged in the controls and only written to `QSettings` on **Save**; Cancel/close discards. Registry labels use `tr()` at build time, so search matches the active language.
- **Setting keys** live in `AppSettings.h` (`fileOps/selectAddedFiles`, `general/language`) — the single source read by both the dialog and the feature code. Read a bool/string live via `QSettings()` where needed.
- **i18n** — `Pixee::installTranslators()` runs in the **constructor before `MainWindow::create()`**, because `tr()` is evaluated at widget-construction time and installing a translator does not retranslate already-built widgets (this was previously done in `run()`, i.e. too late — translations never applied). Language comes from `general/language` (empty = OS locale); the app catalogue loads from `:/i18n` (`Pixee_<lang>.qm`, embedded via `CONFIG += lrelease embed_translations`) and Qt's own from the install's translations dir. Changing language is **restart-to-apply** (no runtime retranslation of the imperatively-built UI). The `.ts` sources live in `translations/`; the embedded resource path stays `:/i18n/Pixee_<lang>.qm` (qmake emits the `.qm` files flat into the build dir, so the source subdirectory doesn't affect it). Empty `.ts` stubs exist for hu/de/fr/es; untranslated strings fall back to English.

### Persistence

`MainWindow` uses `QSettings` (org `Dynart`, app `Pixee`) for `mainWindowGeometry`, `mainWindowState`, the user's tasks-dock visibility intent, and recent Copy/Move destinations. App preferences (`AppSettings.h` keys) share the same store. The SQLite thumbnail DB sits at `~/.pixee/thumbnails.s3db`.

## Qt gotchas hit in this codebase

- **`QListView::IconMode` ignores `setVerticalScrollMode(ScrollPerItem)` for the mouse wheel.** The wheel handler internally uses a small fixed pixel step regardless of the scroll mode. The setting *does* affect the scrollbar arrows and keyboard navigation, just not the wheel. Confirmed unfixed in Qt 6.x. The workaround is `FileListView::wheelEvent` overriding the wheel handler to call `verticalScrollBar()->setValue()` directly with one row per 120-unit `angleDelta()` notch (touchpad `pixelDelta()` is passed through unchanged so smooth scrolling still works). Don't suggest `setVerticalScrollMode` / `singleStep` tweaks for this — they don't take effect.
- **`QString::replace(QString, QString)` mutates the receiver in place** *and* returns a reference to it. Code like `QString out = in.replace(":/", basePath); return info.exists() ? out : in;` is buggy because `in` and `out` end up pointing at the same modified string, killing the intended fallback. Use a local copy first: `QString out = in; out.replace(":/", basePath);`. (Was a real bug in `Theme::realPath` — fixed.)
- **`dataChanged` alone does not make views re-fetch via `Qt::DecorationRole` or custom roles for an existing row** — only `rowsInserted` / `layoutChanged` / `modelReset` re-trigger thumbnail subscription. `FileModel::thumbnailInvalidated` exists for the in-place-modify case so `FileListView::onThumbnailInvalidated` can drop and re-subscribe.
- **`QKeySequence::Quit` and `QKeySequence::Preferences` render as the words "Exit" / "Settings" on Windows** — Windows has no standard shortcut for either, so Qt backs the `StandardKey` with a placeholder whose `toString()` returns the descriptive word, which the menu then paints in the *shortcut column*. So `setShortcut(QKeySequence::Quit)` shows `Quit⋯Exit` and gives no working accelerator. Use an explicit sequence instead (`QKeySequence(Qt::CTRL | Qt::Key_Q)` — `Qt::CTRL` is Cmd on macOS, so it stays cross-platform). Verified via a probe on Qt 6.11: `Quit→"Exit"`, `Preferences→"Settings"`, but `Refresh→"F5"` (a real key, safe to keep). Was a real bug in `createMenus` — fixed.
- **Connecting a signal to a lambda forces the signal's argument types to be complete**, even when the lambda takes no arguments. `connect(_manager, &TaskManager::groupAdded, this, [this]{ ... })` makes Qt instantiate `QMetaTypeId<TaskGroup*>` (to decide whether it can build a metatype array for a possible queued connection), and that static_asserts on `sizeof(T)`: `"Type argument of Q_PROPERTY or Q_DECLARE_METATYPE(T*) must be fully defined"`. A forward declaration in the header is not enough — the `.cpp` must `#include "TaskGroup.h"`. `TaskManager.h` only forward-declares `TaskGroup`, so every file connecting to `groupAdded` needs the include (was a real build break in `TaskStatusWidget.cpp` on Qt 6.4 — fixed). Note this is Qt-version-sensitive: it fires on older Qt 6.x and can compile silently on newer ones, so "it builds on my machine" doesn't mean the include is unnecessary.
- **`QApplication` takes `int &argc` and keeps the reference** — the `argc` it is constructed with must stay alive for the application's whole lifetime, because `QCoreApplication::arguments()` re-reads it on every call. Binding it to a by-value function parameter (as `Pixee::Pixee(int argc, ...)` originally did) leaves a dangling reference the moment the constructor returns. The failure is remote from the cause: Qt calls `arguments()` from the X11 session-manager callback to build the restart command, so an XSMP *SaveYourself* reads a garbage `argc` and runs `strlen` off the end of `argv` — an intermittent `SIGSEGV` a second or two after startup, and only in sessions with `SESSION_MANAGER` set. `Pixee` holds `_argc` as a member for this reason (fixed). Not platform-specific: Windows has no XSMP so the same dangling reference simply stays silent there. Reproduce/bisect with `env -u SESSION_MANAGER ./Pixee` — if the crash vanishes, suspect `arguments()`.
- **Tasks that touch a folder must list it in `affectedDirs()`** so `TaskManager::pathTouched` fires and the UI refreshes. Forgetting this leaves stale rows in the model after a successful operation.
