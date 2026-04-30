# Thumbnail pipeline plan for Pixee

## Goals & non-goals

**Goals**
- Real image thumbnails replace the static `image-big.png` placeholder.
- Once generated, thumbnails are stored locally in `~/.pixee/thumbnails.s3db` so a cell never re-reads from SMB twice.
- Generation is driven by what's visible; scrolling past items doesn't enqueue work for them.
- UI never blocks on disk I/O or image decode.

**Non-goals (defer)**
- Folder thumbnails (composite of first images) — wait for image thumbnails to work first.
- Multi-format thumbnail encoding — start with JPEG only.
- Background pre-warming of the cache for whole folders — visibility-driven only.

## Architecture

Three components, each on its own thread, talking exclusively via Qt signals/slots (queued connections). Same pattern Pixie uses, just with a tighter design.

```
GUI thread                       Cache thread                Generator thread
─────────                        ────────────                ────────────────
FileListView ─subscribe(path)──→ ThumbnailCache ─lookup────→ ThumbnailDatabase
FileModel ←──thumbnailReady───── ThumbnailCache ←─hit────── ThumbnailDatabase
FileListViewDelegate
  paint() reads model               │
                                    │ on miss
                                    ↓
                           ThumbnailGenerator ←─── enqueue ──┘
                                    │
                                    │ QImageReader::setScaledSize → QImage
                                    │
                                    ↓
                                  emits result → ThumbnailCache
                                                   │
                                                   ├→ DB: save
                                                   └→ Model: thumbnailReady
```

Naming follows Pixee/Pixie style:
- `ThumbnailCache` (façade, GUI-thread-callable)
- `ThumbnailDatabase` (lives on its own QThread, SQLite I/O)
- `ThumbnailGenerator` (lives on its own QThread, image decode)

Single generator thread to start. Reasoning: SMB performance with multiple parallel reads of the same share is unpredictable, and one decode at a time keeps memory usage bounded. Add a pool later if measurement shows it helps.

## Cache schema

Table is named `pixee_thumbnails` to avoid colliding with any pre-existing `thumbnails` table from the older Pixie project (the bundled `:/database/thumbnails.s3db` resource is copied into `~/.pixee/thumbnails.s3db` by `Config::_setUpUserFolder`).

```sql
CREATE TABLE IF NOT EXISTS pixee_thumbnails (
    path   TEXT    PRIMARY KEY,
    mtime  INTEGER NOT NULL,
    size   INTEGER NOT NULL,
    width  INTEGER NOT NULL,
    height INTEGER NOT NULL,
    data   BLOB    NOT NULL  -- JPEG-encoded
);
PRAGMA journal_mode=WAL;
```

- `path` is the cache key. `mtime` + `size` are validated on read; if either changed, the entry is stale and the row is dropped + regenerated.
- Storage as JPEG bytes, not raw `QImage` — a 256×256 raw ARGB is ~256 KB; JPEG-encoded is ~5–10 KB. With 10k images that's 50 MB vs 2.5 GB.
- Format always JPEG for now; PNG support deferred until a use case (transparency-aware thumbnails) shows up.

## Request lifecycle

1. **View** — on scroll, resize, or model-rows-inserted, computes the currently visible row range. Diffs against last frame's set; calls `cache->subscribe(path)` for newly-visible, `cache->unsubscribe(path)` for newly-hidden.
2. **Cache (façade)** — keeps a `QHash<QString, int>` of subscribers-per-path. On first `subscribe`, enqueues a DB lookup. On last `unsubscribe`, marks the path "abandoned" but does not actively cancel in-flight work.
3. **DB worker** — runs the SELECT. Hit → emits `thumbnailReady(path, image)`. Miss or stale row → emits `thumbnailMiss(path)` back to the cache façade.
4. **Cache (on miss)** — checks if path is still subscribed. If yes → enqueue in generator. If no → drop.
5. **Generator** — picks the highest-priority pending request (see below). Before doing the expensive part, checks one more time that the path is still subscribed. If yes, opens with `QImageReader`, calls `setScaledSize(thumbnailSize, thumbnailSize)` so the JPEG decoder produces a small image directly (this is the one optimization that matters most on SMB), reads the image, JPEG-encodes to bytes.
6. **Generator (on done)** — emits `(path, image, jpegBytes)`. Cache stores in DB (write-back, fire-and-forget signal) and emits `thumbnailReady(path, image)`.
7. **Model** — has a slot connected to `cache->thumbnailReady`. Stores in `QHash<QString, QImage> _thumbnails`, finds the index for that path, emits `dataChanged(index, index, {DecorationRole})`.
8. **Delegate** — reads `index.data(FileModel::ThumbnailRole)`. If non-null QImage, draws that. Else falls back to `fileItem->pixmap()` (the static placeholder).

The "check subscription before doing expensive work" step at (5) is the cancellation mechanism. No tokens, no aborting mid-decode. If the user scrolls past an item before the generator picks it up, the work is dropped at dequeue time.

## Priority strategy

**Phase 3 design — distance-from-viewport ints**

Each subscribed path has an associated priority `int` representing distance from viewport center (0 = visible, 1 = one row below visible, etc.). On scroll, `FileListView` recomputes the wanted set + distances and tells the cache. The generator's queue is a `std::priority_queue` keyed by distance. Visible items always run before prefetch items. Re-subscribing the same path with a new distance updates priority.

**Phase 1 simplification**: skip the prefetch window entirely on first cut — `FileModel::appendFileItems` subscribes every loaded image at distance 0. Crude but enough to get end-to-end working. View-driven subscription replaces this in Phase 3.

## Phasing

**Phase 1 — Cache plumbing only (no generation).**
- `ThumbnailDatabase` class, owns the SQLite connection on its own thread.
- `ThumbnailCache` façade with `subscribe`/`unsubscribe`/`thumbnailReady`/`thumbnailMiss`.
- `FileModel` listens to `thumbnailReady`, stores in a hash, emits `dataChanged`. Subscribes on `appendFileItems`.
- `FileListViewDelegate` reads `index.data(FileModel::ThumbnailRole)`, falls back to static placeholder.
- Pre-populate the DB by hand to verify end-to-end (e.g., write a one-off helper that walks `resources/icons/*-big.png`, JPEG-encodes them, inserts rows under fake paths matching real files in your test folder; or just run a sqlite3 INSERT manually).
- *Visible at end of phase*: thumbnails appear for items whose paths are in the DB. Other items still show placeholder.

**Phase 2 — Generation.**
- `ThumbnailGenerator` class on its own thread. FIFO queue for now.
- Cache wires misses to generator, generator results back to cache + DB.
- *Visible at end of phase*: real thumbnails appear for any visible image, persist across runs.

**Phase 3 — Visibility-driven priority.**
- `FileListView::updateSubscriptions()` driven by scroll/resize/rows-inserted.
- Replace generator's FIFO queue with priority queue keyed by distance.
- Subscription tracking in cache façade with abandon-on-zero-subscribers.
- *Visible at end of phase*: scrolling fast through a 10k-file folder doesn't queue 10k decodes. Items the user actually pauses on get thumbnails first.

**Phase 4 — Polish.**
- Error placeholder when `QImageReader` fails (use the existing `image-error-big.png`).
- DB schema migration check on startup (in case schema changes later).
- Vacuum / size-cap policy (deferred — let the DB grow until it becomes a problem, then add a LRU eviction).

## Open questions

1. **Where does `mtime` + `size` come from?** Cheap on local disk; on SMB each `QFileInfo::lastModified()` is a network roundtrip. We already have `QFileInfo` cached in `FileItem` from the directory enumeration — reuse that, don't re-stat at thumbnail-request time.
2. **What runs on `~/.pixee/thumbnails.s3db` write contention?** SQLite's WAL mode handles concurrent reader+writer fine. `PRAGMA journal_mode=WAL;` on connect.
3. **Do we want a thumbnail-size config option in the DB?** No for v1 — `thumbnailSize` is hard-coded at 256 in `Config`. If the user later changes it, we'd need a dimensions check on read and regen-on-mismatch. Defer.
4. **Folder thumbnails** — Pixie does "first image inside the folder." Plenty of work on SMB (extra directory listing per folder). Suggestion: defer entirely; folders keep the static folder icon. Can add later if it feels missing.

## Concrete file changes summary

New files (all in project root, matching current flat structure):
- `ThumbnailCache.h` / `ThumbnailCache.cpp` — façade
- `ThumbnailDatabase.h` / `ThumbnailDatabase.cpp` — SQLite thread
- `ThumbnailGenerator.h` / `ThumbnailGenerator.cpp` — decode thread (Phase 2)

Modified files:
- `Pixee.pro` — `QT += sql`; add new sources/headers.
- `Pixee.h` / `Pixee.cpp` — own a `ThumbnailCache*`, expose via getter.
- `FileModel.h` / `FileModel.cpp` — `QHash<QString, QImage> _thumbnails`; slot `onThumbnailReady`; receives cache pointer; subscribes on `appendFileItems`; `data()` returns image for `ThumbnailRole`.
- `FileListView.h` / `FileListView.cpp` — `updateSubscriptions()` slot wired to scrollbar valueChanged + resize + rows-inserted (Phase 3).
- `FileListViewDelegate.cpp` — paint reads `index.data(FileModel::ThumbnailRole)`, draws placeholder if null.
- `MainWindow.cpp` — passes cache to model on construction.
