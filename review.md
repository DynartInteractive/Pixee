# Pixee code review — bug possibilities

Date: 2026-10-06. Scope: everything under `src/` (about 14,000 lines).

**Method.** This is a static read. Nothing was built or run, so every finding is derived from the code, and none has been reproduced. The task layer, thumbnail pipeline and model were read line by line; the UI side (MainWindow, viewer, views, dialogs, task widgets) was covered by three delegated passes whose main claims were then checked against the code. Each finding carries one of these labels:

- **Traced** — the full code path was followed in the source.
- **Qt-dependent** — the code path is traced, but the outcome relies on Qt-internal behaviour recalled from memory, not tested.
- **Reported** — found by a delegated pass and not independently re-checked.

Line numbers refer to the tree as of this date.

## Fix status (2026-10-06)

**Rebased onto 0.5.0:** the review and the fixes were made against an older (0.2.0-era) checkout, and the line numbers below refer to it. The fixes were then rebased onto 0.5.0 and released as 0.5.1. During the rebase, the save path was adapted to 0.5.0's colour adjustments. A successful save now folds a pending adjustment into the pixels (`ViewerWidget::markSaved`), since clearing the dirty flag alone left the viewer "modified". Discard also resets the adjustment.

Most findings below are fixed. The app and all 13 test suites build with MinGW / Qt 6.11.1, and all 174 tests pass. Of those, 12 are new regression tests: move onto itself, case-only move, Save As onto the source, saving to an unwritable format, `.jfif` saving, case-only batch-rename planning, `{n}` and width edge cases, `isSameFile` / `moveAside`, and a destination inside the source tree.

**Not run:** the app was not started (launching an exe from the network drive blocks on a Windows prompt here). The Linux-only change (M22) is compiled out on Windows. The UI fixes are compile-checked only.

| Finding | Status |
|---|---|
| H1–H4 | **Fixed.** `FileOpsHelpers::isSameFile` guards every task. Overwrite no longer deletes first: copy and image writes use `QSaveFile` (`ImageFormats::writeImage`), while move and rename set the old file aside and restore it on failure. `doMove` refuses moves into the item's own folder. Batch rename and rename compare names case-insensitively on Windows / macOS. Save is refused for unwritable formats and maps aliases. The dirty flag is cleared only when the save task succeeds. |
| H5 | **Fixed.** `QSaveFile::commit()` is checked and the size verified before the source is removed. The fast path uses `QDir::rename`, because `QFile::rename` has its own unchecked copy fallback. Moves keep the modification time. |
| M1–M5, M7–M14, M17–M21 | **Fixed** as described in each finding. |
| M6 | **Fixed** differently from the suggestion: a drop from another process is always answered with Copy, because Pixee's own task deletes the sources. Drops respect `possibleActions()`. |
| M15 | **Mostly fixed.** The guard resolves symlinks and case. A mapped drive against its UNC path is still not recognised, but `expandToFiles` now walks the tree before creating anything, so the runaway can't happen. |
| M16 | **Partly fixed.** Symlinked files are now copied and moved. Symlinked folders are still skipped, and the cleanup no longer removes them (`NoSymLinks`). |
| M22 | **Fixed** (untested: compiled out on Windows). |
| Low items | **Fixed:** shutdown list iteration, group deletion during `execute()`, `resolveOrAsk` ordering, zero-size scaling, exit waiting on decodes, the negative cache keyed by mtime and size, second-subscriber replies, all EXIF orientations, `createFolder` on an un-enumerated folder, the path-box backslash comparison, case and `..` in the command-line path, the dock-visibility flags, `Pixee::exit` deleting the window in `closeEvent`, the unparented models, kick over-firing, `rowsRemoved`, crop focus, zero-size paint, the debug `qBound` assert, name validation in all name dialogs, the `{n:huge}` width, `{n}` inside names, folder names split at a dot, the Cut cleared on an empty paste, toasts overwriting each other, and `&` / drive-root menu labels. Also fixed: delete through a symlink only removes the link, and a superseded viewer decode is no longer delivered. |
| **Not changed** | A paused or prompting task still holds a worker thread (design). The thumbnail DB is not pruned. A stale embedded EXIF thumbnail is not detected. `goToPathFromLineEdit` still enumerates synchronously. Drives are read once. A failed enumeration still looks like an empty folder. `{n}` follows selection order. The UI still builds one widget per task, and `expandToFiles` runs on the GUI thread. The first pass of `tryExpandWindow` still walks from row 0. A fixed-ratio marquee smaller than one ratio unit is still cropped off-ratio. |

## Summary

The most important result is a family of four ways to permanently lose files, all with the same root cause: on "Overwrite", tasks delete the destination first, without checking that the destination is the source and before the replacement exists.

| # | Severity | Finding |
|---|---|---|
| H1 | High | "Move to" the folder a file is already in deletes it on Overwrite |
| H2 | High | Save As with the dialog defaults deletes the source on Overwrite |
| H3 | High | Batch rename that only changes letter case deletes the files on Overwrite (Windows, macOS) |
| H4 | High | File → Save deletes the original when the write fails, and the edit is already marked clean |
| H5 | High | Cross-volume move can remove the source after an incomplete copy |
| M1 | Medium | Viewer cache shows pre-edit pixels after Save; a second save reverts the first |
| M2 | Medium | Unsaved edits are dropped without a prompt on window close and folder navigation |
| M3 | Medium | A duplicate image-load result wipes an edit just made |
| M4 | Medium | Crash when a task group finishes while its Abort confirmation is open |
| M5 | Medium | `FileItem*` held across a modal dialog can dangle |
| M6 | Medium | Shift-drag between two Pixee windows deletes sources while the other window is still moving them |
| M7 | Medium | Thumbnail request is lost when re-queued while a worker still holds the same path |
| M8 | Medium | `_activeJobs` is not cleared on two early returns, stalling background fill |
| M9 | Medium | In-memory thumbnails are never evicted |
| M10 | Medium | EXIF parser reads out of bounds on a malformed JPEG |
| M11 | Medium | "Delete" silently becomes a permanent delete where there is no trash |
| M12 | Medium | Images over Qt's 256 MB allocation limit fail to open, silently |
| M13–M22 | Medium/Low | See below |

Suggested order of work: H1–H4 share one fix and should go first, then H5, M4, M10, M1–M3.

---

## High — permanent data loss

### H1–H3: "Overwrite" deletes the file it was meant to keep

**Root cause.** `MoveFileTask`, `RenameTask`, `ConvertFormatTask` and `ScaleImageTask` all do this:

```cpp
if (QFile::exists(_dst)) {
    ... resolveOrAsk(DestinationExists, ctx) ...
    case Overwrite:
        if (!QFile::remove(_dst)) { ... }
```

None of them checks whether `_dst` is the same file as `_src`. `CopyFileTask.cpp:54-58` does have that check (via `canonicalFilePath()`); the others do not. When source and destination are the same file, the conflict prompt shows the file against itself, and Overwrite deletes it. The delete is direct, not to the trash.

**H1 — Move to the same folder.** *Traced.*
- `FileOpsMenuBuilder.cpp:447-493` (`doMove`) has no same-folder guard; `expandToFiles(src, destFolder)` produces `src == dst`. The drop/paste path has the guard (`FileOpsMenuBuilder.cpp:277-282`), the menu path does not.
- `MoveFileTask.cpp:85-109` then removes `_dst` and fails both `QFile::rename` and the copy fallback.
- Scenario: `lastMoveToPath` is folder X. Browse X, right-click a file, choose `Move to "X"` (or `Move to...` and pick the current folder), answer Overwrite. With a multi-selection and "apply to all", every selected file is deleted.
- Folder variant: moving `X/A` into `X` passes `destIsSourceOrDescendant`, every file maps onto itself, and `FolderCleanupTask` then removes the emptied directories.
- The drop/paste guard is a case-sensitive string compare of cleaned paths (`FileOpsMenuBuilder.cpp:277-278`). The same folder reached two ways — a mapped drive and its UNC path, a case difference, a junction — is not recognised, and a Shift-drop then lands in this same bug.

**H2 — Save As with the defaults.** *Traced.*
- `SaveAsDialog.cpp:35-37, 51, 59-62, 111-115`: the defaults are the source folder, the source base name and the source format, so `destPath()` is the source path. `validate()` only checks that the name is non-empty.
- `MainWindow.cpp:951-978` has no `dst == src` check and enqueues `ConvertFormatTask(src, dst, ...)` for an unedited image.
- `ConvertFormatTask.cpp:42-43` removes `_dst` before `QImageReader reader(_src)` at line 57. The read fails and the image is gone.
- Scenario: Ctrl+Shift+S, change only the quality slider (re-compress in place), Save, Overwrite.
- Independent of `src == dst`: removing the destination before the source has even been decoded means any decode failure destroys the existing destination for nothing. `ScaleImageTask.cpp:41-58` has the same ordering.

**H3 — Case-only batch rename.** *Traced; assumes a case-insensitive filesystem.*
- `BatchRenameDialog.cpp:167-168`: for `a.jpg → A.jpg`, `QFileInfo::exists()` is true (it is the same file) and the case-sensitive `vacatedOldNames` lookup misses, so the row is marked "exists, you'll be asked" and OK stays enabled.
- `RenameTask.cpp:31` compares `_src == _dst` as strings, so the pair passes; lines 46-47 then remove the file itself and `QFile::rename` fails.
- Scenario: Tools → Batch rename, Find `IMG`, Replace `img`, Overwrite + apply to all. Every selected file is deleted. Skip and Rename do not produce the wanted name either, so a case-only batch rename cannot succeed at all.
- `RenameDialog.cpp:43-44` blocks the single-file case-only rename with "already exists here" — safe, but it means the rename is impossible there too. *(Reported.)*

**Fix direction for H1–H3.**
1. In each task, before prompting, compare source and destination as files, not strings (`canonicalFilePath()`, or `QFileInfo::operator==`). Same file → no-op for move/convert; for rename, a case-only change should call `QFile::rename` directly, which handles it.
2. Never delete the destination first. Write to a temporary name in the destination folder and rename over the target once the write has succeeded (`QSaveFile` does this for the image writers).
3. Add the same-folder guard to `doMove`, and reject `dst == src` in `SaveAsDialog::validate()`.
4. Add tests: `tests/MoveFileTask` and `tests/ImageTasks` have no `src == dst` case, and `tests/BatchRename` has no case-only case.

### H4 — File → Save can destroy the original and the edit together

*Traced.*
- `MainWindow.cpp:914`: the format is the raw file suffix. `MainWindow.cpp:1006` enables Save on `isModified()` alone, with no check against `Config::writableImageFormats()`.
- `SaveImageTask.cpp:53` removes the original, then `QImageWriter` (line 67-72) fails for any format Qt can read but not write.
- `MainWindow.cpp:927` clears the dirty flag immediately after enqueueing, before the task has run.
- Scenario: open a `.gif` (stock Qt has no GIF writer) or a `.jfif` — `ScaleImageTask.cpp:79-87` documents that a bare `jfif` format name fails and maps it through `ImageFormats::aliasedFormat`, but this path does not. Press `R`, Ctrl+S, confirm. The original is deleted, the write fails, and nothing prompts again. Via the unsaved-changes guard (`MainWindow.cpp:945`) the viewer has already moved on, so the edited pixels are gone as well.
- Any other write failure (disk full, read-only share, encoder limits) ends the same way.
- `saveImageAs` clears the flag early too (`MainWindow.cpp:983`): a Skip in the conflict prompt or a failed write leaves the edit marked clean.

Fix direction: disable Save for unwritable formats and route the suffix through `aliasedFormat`; write through a temporary file; clear the dirty flag from the task's success signal, not at enqueue time.

### H5 — Cross-volume move can remove the source after an incomplete copy

*Qt-dependent.*
- `MoveFileTask.cpp:70-73`: after the copy loop, `out.close()` is called and its outcome is never inspected, then `QFile::remove(_src)` runs.
- `QFile` buffers small writes. The 64 KB chunks go straight through, but a final chunk smaller than the buffer is only written by `close()`. If that flush fails (disk full, a network share dropping), `close()` has no return value to report it, and the task carries on to delete the source.
- Network filesystems can also defer a write error to close time even when every `write()` returned success.
- Result: a truncated destination and a deleted source. `CopyFileTask.cpp:113` has the same unchecked close, with the milder outcome of a truncated copy reported as success.

Fix direction: call `out.flush()` and check it (or check `out.error()` after `close()`), and compare the destination size with the source size before removing the source.

Related, lower severity: the manual copy loop does not carry over the modification time, so a cross-volume move changes a file's date and its position under `Sort by → Modified`.

---

## Medium

### M1 — Viewer cache shows pre-edit pixels after Save; a second save reverts the first
*Traced.* `saveEditedOverOriginal()` (`MainWindow.cpp:905-929`) never updates or evicts `_viewerImageCache[path]`; the cache is only touched on load, removal, dismiss and rename. Crop image A, Ctrl+S, Next, Prev: the cache hit at `MainWindow.cpp:1444-1447` shows the uncropped original although the disk holds the crop. Rotate and save again and the disk file becomes the rotated, uncropped image. Fix: on save, put the edited image into the cache (or evict the entry).

### M2 — Unsaved edits dropped without a prompt
*Traced.*
- `MainWindow.cpp:1809-1811`: `closeEvent` calls `_pixee->exit()` with no `maybeDiscardEdits()` and no way to ignore the event. Closing the window with a pending edit loses it. Running tasks are also stopped with no confirmation, which can leave a multi-file move half done.
- `MainWindow.cpp:669-675`: `navigateTo` leaves viewer mode with `_viewerWidget->clear()` and no guard. It is reachable while the viewer is up, for example by clicking a folder in the tree dock.
- That branch also skips the rest of `dismissViewer()`'s cleanup: the path list, index and image cache are not cleared, the load abort version is not bumped, and the window title keeps the image name. *(The cleanup detail is Reported.)*

### M3 — A duplicate load result wipes an edit just made
*Traced.* `onImageLoaded` (`MainWindow.cpp:1516-1518`) calls `setImage` whenever the path matches the current image, and `setImage` resets the edit buffer, dirty flag, crop mode and zoom (`ViewerWidget.cpp:64-70`). `ImageLoader.cpp:57-79` checks for abort before the decode but not after, so a superseded load that is already decoding still emits `loaded`. `MainWindow.cpp:1461` issues a new load on every cache miss even when one for the same path is in flight.

Scenario: go A → B → A within A's decode time, or press Next while the preload of the next image is decoding. The first result shows the image; the user rotates or starts a crop; the second result arrives and the edit vanishes with no prompt. Fix: skip `setImage` when the viewer already shows the full-resolution image for that path.

### M4 — Crash when a group finishes while its Abort confirmation is open
*Qt-dependent.* `TaskItemWidget.cpp:53-61` and `TaskGroupWidget.cpp:80-88` call `QMessageBox::question(this, ...)` and then `emit` on `this`. `TaskDockWidget.cpp:72-77` does `gw->deleteLater()` on `groupRemoved`. If the group completes while the box is up, the deferred delete runs inside the box's own event loop: the group widget is destroyed, taking the row and its child message box with it — and that message box is a stack object inside `QMessageBox::question`. The lambda then resumes on a destroyed `this`.

Scenario: click Abort on a short copy and hesitate. Fix: parent the box to `window()` and hold a `QPointer` to `this` across the call.

### M5 — `FileItem*` held across a modal dialog can dangle
*Traced.* `MainWindow.cpp:1940` fetches `item`, line 1956 runs `dlg.exec()`, line 1960 calls `renameItem(item, ...)`. `createFolderIn` does the same with `parent` (`MainWindow.cpp:1970-1994`). While the dialog is open, a refresh can remove the row — `applyRefreshDiff` deletes the `FileItem` (`FileModel.cpp:416-419`) — after a running task, another application, or the refresh fired on window activation. Fix: re-resolve by path after `exec()`.

### M6 — Shift-drag between two Pixee windows deletes sources too early
*Traced in the code; the two-instance scenario is not tested.* After a drag, `FileListView.cpp:475-478` and `FolderTreeView.cpp:185-188` enqueue a permanent delete of the sources when the result is Move, Shift is held and `drag->target()` is null (the drop landed in another process). A second Pixee instance as receiver enqueues asynchronous `MoveFileTask`s and returns immediately, so the source window's delete races the receiver's moves. For a cross-volume move, or while the receiver is showing a conflict prompt, files not yet copied can be deleted. Nothing prevents two instances from running.

Related: in `FileListView.cpp:617-618` and `FolderTreeView.cpp:229-230`, `event->setDropAction(action)` is followed by `event->acceptProposedAction()`, which resets the drop action to Qt's proposed one; the reported action can therefore differ from what Pixee actually did. *(Qt-dependent.)*

### M7 — Thumbnail request lost when re-queued while a worker still holds the path
*Traced.* `ThumbnailGenerator::dispatch` pops queue items and discards any whose path is still in `_processing` (`ThumbnailGenerator.cpp:124-130`); the item is not re-queued. When the old worker then aborts, `ThumbnailCache::onGenerationAborted` (`ThumbnailCache.cpp:220-227`) clears `_inGen` for the path and emits nothing.

So if a path is cancelled and re-requested while its worker is still in the chunked-read phase, the new request is dropped and no terminating signal is ever sent. This is the invariant CLAUDE.md states for `_activeJobs`: the subscriber waits forever, that cell keeps its placeholder, and background fill stops.

Scenarios, all more likely on a slow share: scroll a large file out of the window and back; leave a folder and return quickly; or use "Refresh thumbnail" twice in a row (`ThumbnailCache.cpp:144-149` cancels and re-enqueues in the same call).

Fix direction: when a worker finishes or aborts a path that still has an entry in `_currentPriority`, push it back on the queue.

### M8 — `_activeJobs` not cleared on two early returns
*Traced.* `FileListView.cpp:204-208` and `238-242` unsubscribe everything and clear `_lastSubscribed`, but leave `_activeJobs` alone. `ThumbnailCache::unsubscribe` guarantees no later signal for those paths, so the stale entries never drain, `onCacheJobDone` never reaches `tryExpandWindow`, and the kick at line 345 is blocked by `_activeJobs.isEmpty()`. Background fill is dead until the next `setRootIndex`. Fix: `_activeJobs.clear()` in both branches.

### M9 — In-memory thumbnails are never evicted
*Traced.* `FileModel::onThumbnailReady` stores every delivered image in `_thumbnails` (`FileModel.cpp:522`). Entries are removed only when the row itself is removed, modified, renamed or fails. Changing folder does not clear them, and background fill covers the whole folder.

A 256 px thumbnail decoded to 32-bit is about 175–260 KB, so a 10,000-image folder holds roughly 1.7–2.6 GB, and the total accumulates across every folder visited in the session. Fix direction: bound the map (LRU by count or bytes), or keep only the current folder plus folder-index images.

### M10 — EXIF parser reads out of bounds on a malformed JPEG
*Traced.* In `ThumbnailWorker.cpp`:

```cpp
const quint32 ifd0Offset = read32(tiff + 4, le);
if (ifd0Offset + 2 > quint32(tiffLen)) return result;      // line 88
...
if (ifd1Offset == 0 || ifd1Offset + 2 > quint32(tiffLen))  // line 108
```

Both additions are 32-bit unsigned and wrap. An offset of `0xFFFFFFFE` or `0xFFFFFFFF` wraps to 0 or 1, passes the check, and `read16(tiff + ifd0Offset, ...)` then reads about 4 GB past the buffer. A corrupted or crafted JPEG crashes the application as soon as its folder is browsed. Fix: compare without the addition (`ifd0Offset > quint32(tiffLen) - 2`) or widen to `quint64`, as line 121 already does for the thumbnail range.

### M11 — "Delete" silently becomes a permanent delete where there is no trash
*Traced.* `DeleteFileTask.cpp:33-49`: when `QFile::moveToTrash` returns false the task falls through to `removeRecursively()` / `remove()`. The confirmation (`FileOpsMenuBuilder.cpp:512-516`) said "Delete", not "Permanently delete". On network shares — the environment this application is built for — the trash is normally unavailable, so this is the usual path there, including for whole folders. Fix direction: detect the case up front, or have the task ask before the hard delete.

A related risk, *Qt-dependent*: `info.isDir()` is true for a symlink to a directory, and `QDir(_path).removeRecursively()` on such a path would delete the contents of the link's target.

### M12 — Images over Qt's allocation limit fail to open, silently
*Qt-dependent.* No `QImageReader::setAllocationLimit` call exists anywhere in `src/`. Qt 6 rejects any image whose decoded form exceeds 256 MB (about 64 megapixels at 32 bpp). `ImageLoader.cpp:73-77` then emits `failed`, and `MainWindow::onImageLoadFailed` (`MainWindow.cpp:1524`) only logs. The viewer stays on the upscaled thumbnail with no message, and the edit keys silently do nothing because the placeholder is still showing. Any other open or decode failure ends in the same silent state.

Related: both `ImageLoader.cpp:34-53` and `ThumbnailWorker.cpp:197-201` read the entire file into memory before decoding. A multi-gigabyte file can throw `std::bad_alloc` on a worker thread, where nothing catches it.

### M13 — Rename does not update an item's type
*Traced.* `FileModel::renameItem` (`FileModel.cpp:745-747`) replaces the `QFileInfo` but `FileItem::_fileType` is fixed at construction. Renaming `photo.jpg_` to `photo.jpg` leaves a plain file with no thumbnail that cannot be opened in the viewer; the reverse leaves a broken image cell. The follow-up refresh does not fix it, because `computeDiff` matches by path and compares only mtime and size (`FileModel.cpp:345-351`). It stays wrong until restart. Fix: when the classification changes, remove and re-insert the row.

### M14 — Menu Move/Copy of an empty folder half-happens
*Traced.* `FileOpsMenuBuilder.cpp:474` and `:429` return when `pairs` is empty, but `expandToFiles` (`FileOpsHelpers.cpp:38-49`) has already created the destination directory tree. Moving a folder that contains no files creates the tree at the destination, leaves the source in place, and triggers no refresh. The drop/paste path handles this correctly (`FileOpsMenuBuilder.cpp:321`).

### M15 — The "into itself" guard is lexical
*Traced for the guard; the runaway is Qt-dependent.* `FileOpsHelpers.cpp:18-23` compares cleaned path strings, case-sensitively. A mapped drive against its UNC path, a junction, `subst`, or a case difference on Windows defeats it. `expandToFiles` then creates the destination inside the tree it is about to walk, on the GUI thread, and can keep nesting until the path length limit stops it.

### M16 — Folder moves and copies silently skip symlinks and shortcuts
*Reported.* Both iterators in `FileOpsHelpers.cpp:42-53` use `QDir::NoSymLinks`; on Windows that also excludes `.lnk` files. A folder copy omits them without notice; a folder move leaves them behind, so the source folder survives.

### M17 — Viewer path list is not updated when a task renames or moves the viewed file
*Reported.* `TaskManager::pathMoved` is wired only to the thumbnail cache (`MainWindow.cpp:428`); `onPathRenamed` is driven only by `FileModel::pathRenamed`. Batch rename is reachable from the menu bar while the viewer is up. Afterwards `_viewerImagePaths[_viewerIndex]` is a dead path, so Save writes a new file under the old name and Delete, Rename and Save As act on a path that no longer exists.

### M18 — Save As is enabled from the selection but acts on the current index
*Reported.* `MainWindow.cpp:999-1000` gates on "exactly one image selected"; `currentContextImagePath()` (`MainWindow.cpp:1542`) uses `currentIndex()`. When the two differ (rubber-band selection, Ctrl-click), Save As opens over a different image than the one selected.

### M19 — Crop mode is not isolated from wheel, context menu or resize
*Wheel traced; the rest Reported.* `ViewerWidget::wheelEvent` (`ViewerWidget.cpp:934-949`) has no crop-mode check: a plain wheel tick changes image and discards the marquee, Ctrl+wheel zooms underneath it. Rotate/Flip from the context menu during a crop leaves the marquee over a different region, and a resize in a fit mode moves the image under a stationary marquee.

### M20 — Viewer wheel acts on every event
*Traced.* The same handler emits prev/next (or zooms) for every wheel event with a non-zero delta, with no accumulation to 120 units and no use of `pixelDelta()`. A precision touchpad sends many small events per gesture, so one swipe skips many images.

### M21 — Drag hover-expand and edge auto-scroll never run
*Qt-dependent.* The `dragEnterEvent` / `dragMoveEvent` overrides in `FolderTreeView.cpp:68-75, 98-128` and `FileListView.cpp:514-554` never call the base class. Qt arms the tree's auto-expand timer and the views' auto-scroll in those base implementations, so `setAutoExpandDelay(600)` and `setAutoScroll(true)` (`FolderTreeView.cpp:59-60`) have no effect — contrary to the comment at lines 48-51. You cannot drop into a collapsed subtree or scroll by dragging to the edge.

### M22 — Linux/macOS: file metadata is read lazily on the GUI thread
*Qt-dependent.* `FolderEnumerator` and `FolderRefresher` emit the `QFileInfo` list straight from `entryInfoList`. On Windows those objects arrive with size and timestamps filled in. On Unix they generally do not: the first `lastModified()`, `size()` or `birthTime()` call performs the `stat`. Those first calls happen on the GUI thread in `computeDiff` (`FileModel.cpp:347-348`, for every file on every refresh), in the date-sort comparator (`FileFilterModel.cpp:91-96`), and at subscribe time. On a network mount that defeats the off-thread enumeration. Fix: call `QFileInfo::stat()` on each entry in the worker before emitting.

---

## Low

**Task layer** *(all Traced)*

- **Shutdown iterates a list it mutates.** `TaskManager::shutdown` (`TaskManager.cpp:38`) loops over `_groups` calling `stopAll()`. For a group with no running task, every task aborts synchronously and `maybeRemoveGroup` calls `_groups.removeAll(group)` (line 203) inside the loop. The next group in the list is then skipped; if it has a task waiting on a conflict answer, `thread->wait()` never returns. Iterate over a copy.
- **Group can be deleted while a worker is still inside `execute()`.** `Task::execute` sets the terminal state, then emits (`Task.cpp:154-156`). If the user stops the group's remaining queued tasks in that window, `maybeRemoveGroup` sees all tasks terminal and schedules the delete while the worker is still using the task. The window is microseconds wide.
- **Possible lost wake-up in `resolveOrAsk`.** `Task.cpp:111-116` emits `needsAnswer` before locking and resetting `_pendingAnswer`. An answer that arrived first would be erased and the task would wait forever. A human cannot answer that fast; an automated answer could.
- **A paused or prompting task holds its worker thread.** With two workers, two groups that are paused mid-task (or waiting on a prompt) stop every other group.
- **Overwrite is not atomic in `CopyFileTask`.** The existing destination is removed before the copy starts (`CopyFileTask.cpp:59`); a failure or cancel leaves neither file.

**Thumbnail pipeline** *(all Traced)*

- **Extreme aspect ratios fail.** `ThumbnailWorker.cpp:267-268`: `originalSize.scaled(...)` rounds a 1024×1 strip to 256×0, and the decode returns a null image. Clamp both dimensions to at least 1.
- **Exit waits for in-flight reads.** `ThumbnailGenerator::~ThumbnailGenerator` (`ThumbnailGenerator.cpp:29-37`) quits and joins the workers without bumping `_abortVersion` first.
- **Negative cache is keyed by path only.** A file that failed while half-written and has since changed (`applyRefreshDiff` sees it as modified) is still answered with a miss (`ThumbnailCache.cpp:72`). Keying `_failures` by path + mtime + size would make the manual "Refresh thumbnail" unnecessary in that case.
- **The thumbnail database is never pruned.** There is no delete path in `ThumbnailDatabase`; rows for deleted files and for files under a renamed or moved folder stay forever.
- **`subscribe` emits nothing for an already-delivered path.** A second subscriber to a path that has already resolved (`ThumbnailCache.cpp:80-94`, `count > 1`, not in flight) never gets a signal. Callers need their own fallback.
- **Embedded EXIF thumbnails.** Mirrored orientations (2, 4, 5, 7) are ignored for the embedded-thumbnail path but honoured by the full decode, and an embedded thumbnail can be stale if another program edited the image without updating it.

**Model** *(Traced unless noted)*

- **`createFolder` on a folder that has not been enumerated** (`FileModel.cpp:838-864`) appends next to the Loading placeholder. The follow-up refresh adds the real entries, but the placeholder stays and the folder never gets its `..` row, since `..` only comes from the initial enumeration. Reachable only if New Folder can target a folder that was never opened.
- **`goToPathFromLineEdit` enumerates synchronously.** `expandPath` (`FileModel.cpp:871-919`) calls `info.exists()` and the synchronous `appendFileItems` for each path segment on the GUI thread; typing a path on a slow share blocks the window.
- **Drives are read once.** `populateDrives` runs only in the constructor, so a drive connected later never appears.
- **A failed enumeration looks like an empty folder.** An unreachable share returns an empty list, with no `..` row and no message.

**MainWindow and shutdown** *(Reported)*

- `onPathRenamed` (`MainWindow.cpp:2033-2034`) compares the path box text, which uses backslashes on Windows, against forward-slash paths, so it never matches; the saved `lastPath` can keep a stale name.
- The command-line image path is compared exactly (`MainWindow.cpp:605-608`) while the restore chain is case-insensitive; `Pixee c:\photos\a.jpg` against `C:\Photos` opens the folder but not the viewer.
- Tasks-dock visibility: `_suppressDockVisibilitySync` can stay set when visibility does not change (`MainWindow.cpp:498-499`), and `metadataDockEnabled` is overridden by `restoreState`.
- `Pixee::exit()` deletes the main window from inside its own `closeEvent` (`Pixee.cpp:67-79`). `_fileModel` and both proxy models are created without a parent (`MainWindow.cpp:72, 80, 86`) and never deleted, so `FileModel`'s destructor — which stops its two threads — never runs.

**Views and viewer** *(Reported)*

- Kick and expansion can over-fire: each empty pass inside `tryExpandWindow`'s loop also posts a queued `tryExpandWindow` (`FileListView.cpp:345-348`), and each pass walks rows from 0, which goes quadratic on long runs of non-image rows.
- `rowsRemoved` does not reschedule subscriptions, so rows that shift into view after a delete are picked up only on the next scroll or resize.
- Focus is not returned to the viewer when crop ends from the crop bar (`ViewerWidget.cpp:182-190`).
- A fixed-ratio marquee smaller than one reduced ratio unit is cropped off-ratio (`ViewerWidget.cpp:228`); images that scale to a zero dimension are not painted (`ViewerWidget.cpp:488-498`).

**Dialogs and file operations** *(Reported)*

- `SaveAsDialog` accepts any non-empty name: separators and `..` escape the chosen folder, and a colon on NTFS writes an alternate data stream.
- `RenameDialog` and `NewFolderDialog` accept trailing dots and spaces and reserved device names.
- `BatchRenamePlan`: `{n:999999999}` allocates a billion-character string per preview row; a literal `{n}` inside a file name is numbered; folder names are split at the last dot; `{n}` follows selection order, not display order.
- After a Cut, the clipboard is cleared even when the paste did nothing (`FileOpsMenuBuilder.cpp:246-248`).
- One widget row is built per task, and `expandToFiles` walks the source tree twice on the GUI thread; a 20,000-file operation freezes the window while it is set up.

**Documentation**

- `Config::maxThreadCount()` returns 2 (`Config.cpp:75-80`); CLAUDE.md says 4 in two places.

---

## Checked and found sound

- **Stop, pause and answer plumbing in `Task`** — the `_completionEmitted` claim, the wake-ups on stop, and one terminal signal per task are consistent.
- **Sequential-within-group dispatch** — at most one task per group runs, so sticky answers do not race.
- **Copy onto itself** — `CopyFileTask` compares canonical paths before removing on Overwrite.
- **`FolderCleanupTask`** — the absolute-path `rmdir` fix is in place, and length-descending order does remove children before parents.
- **Refresh coalescing** — `_refreshPending` / `_refreshAgain` / per-folder versions in `FileModel` hold together, including a folder renamed mid-refresh.
- **Refresh diff** — row removal in descending order with a path re-check is correct.
- **Negative-cache miss** — emitted, and queued, as CLAUDE.md requires.
- **Thumbnail DB access** — single connection, used only on its own thread.
- **Viewer prev/next/removal indexing, zoom index bounds, crop rectangle clamping** — in range.
- **Conflict prompter** — FIFO with a busy guard; queued questions are retracted; close defaults to Skip.
- **Task widgets** — store ids, not task pointers; no worker-thread signal reaches a widget directly.
- **Open With** — launches with an argument list, no shell.
- **Documented gotchas** — the `Theme::realPath` copy, the `_argc` member, the explicit Quit/Settings key sequences and the scroll reset in `setRootIndex` are all present.
