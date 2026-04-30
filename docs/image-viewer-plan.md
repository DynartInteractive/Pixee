# Image viewer plan

Pixee currently has no way to actually look at an image — double-clicking an
image cell does nothing. This adds a fullscreen-capable viewer **inside the
existing `MainWindow`** (no second QMainWindow — Pixie's two-window design
was a known pain point).

## Goals

- Activate an image (double-click or Enter on an image cell) → the central
  area swaps from the file list to the viewer.
- Esc / Enter / double-click on the viewer returns to the file list.
- Navigate between images in the current folder with ← / → and the mouse
  wheel.
- Pan + zoom with mouse and keyboard.
- F11 toggles fullscreen — chrome (menu, dock, path bar, status bar) hides;
  central viewer fills the window. Toggling back restores the previous
  layout.
- Progressive loading on SMB: while the full image is streaming, show the
  cached thumbnail as a placeholder so there's never a blank rectangle.
- EXIF auto-rotation; left/right rotate keys.
- "Copy to..." with last-path memory.

## Non-goals (deferred)

- Slideshow mode.
- Multi-image tabs (the user explicitly doesn't want them; can be added
  later if a reason emerges).
- Duplicate-image finder, batch operations — separate feature.

## Architecture

### Widget structure

`MainWindow`'s central widget today is a vertical layout: **path edit** on
top, **file list view** below. Wrap the *file list view* (and only the file
list view — the path edit stays put) in a `QStackedWidget` with two pages:

- Page 0 — `_fileListView` (the existing file grid).
- Page 1 — `_viewerWidget` (new — the image canvas).

The path bar continues to drive folder navigation; in viewer mode it shows
the current image's path and Enter-on-the-line still navigates folders, so
typing a folder path closes the viewer and shows that folder's grid.

```
MainWindow
├── menuBar
├── (dock) folder tree
├── central
│   ├── pathLineEdit
│   └── QStackedWidget
│       ├── Page 0 — FileListView
│       └── Page 1 — ViewerWidget
└── statusBar
```

### New types

- **`ViewerWidget`** (`QWidget`) — the rendering surface. Owns:
  - `QImage _placeholder` — the thumbnail used while the full-res streams.
  - `QImage _full` — the full-res, populated as rows arrive.
  - `_zoomLevel`, `_translate`, `_fit` — viewport state.
  - `paintEvent` — composites the best available image (placeholder
    upscaled if `_full` is null/incomplete; full image otherwise) at the
    current zoom + pan + EXIF orientation.
  - Mouse: drag-to-pan, wheel-to-zoom (with optional Ctrl modifier),
    double-click → emits `dismissed()`.
  - Keys: ← / → emit `prevImage()` / `nextImage()`; Esc / Enter emit
    `dismissed()`; F11 emits `fullscreenToggled()`; +/- zoom; 0 fit; 1
    actual size.

- **`ImageLoader`** (`QObject`, lives on its own `QThread`, in a later
  phase) — reads the file in chunks (same pattern as `ThumbnailWorker`) and
  emits row-by-row deltas. For Phase 1 we'll load synchronously; Phase 4
  introduces the worker.

### Lifecycle

- `MainWindow::activateImage(FileItem*)` is called from
  `goToFolderByFileIndex` (or rather, from a new branch in the activated
  handler that runs for `FileType::Image` rows). It:
  1. Builds the navigation list from the current folder's image children
     (in the file-list's sort order) and remembers the current index.
  2. Pulls the cached thumbnail from `FileModel` (already there in
     `_thumbnails`, keyed by path).
  3. Switches the stack to Page 1.
  4. Asks `ImageLoader` for the full-res (Phase 4 — Phase 1 just calls
     `QImageReader::read()` synchronously).

- `MainWindow::dismissViewer()`:
  1. Switches the stack back to Page 0.
  2. Restores keyboard focus to the file list view.
  3. If the user navigated images while in the viewer, optionally
     re-selects the last-viewed image's cell in the file list.

- `MainWindow::toggleFullscreen()`:
  1. In fullscreen, hide menu bar / status bar / folder tree dock / path
     edit so the viewer fills the window.
  2. In windowed, restore them. (Save/restore visibility state — don't
     unconditionally show, since the user might have hidden the dock.)
  3. `showFullScreen()` / `showNormal()` on `MainWindow`.

## Phases

### Phase 1 — basic viewer

- Add `ViewerWidget` skeleton with paintEvent that fits a single
  pre-loaded `QImage` to its viewport (no pan/zoom yet — fit only).
- Wrap `_fileListView` in a `QStackedWidget`; add `_viewerWidget` as page 1.
- New `MainWindow::activateImage(FileItem*)` and `dismissViewer()`.
- Hook the existing `activated` connection to detect `FileType::Image`
  and call `activateImage`.
- Synchronous full-res load with `QImageReader::read()` + `setAutoTransform(true)`.
- Esc / double-click / Enter dismisses.
- *Visible at end of phase*: double-clicking a thumbnail shows the full
  image in the central area; Esc returns. No navigation, no panning,
  blocks briefly on load for big images on SMB.

### Phase 2 — folder navigation

- `MainWindow::activateImage` builds an ordered list of image paths in
  the current folder using `FileModel::indexFor` to walk children of the
  current folder, filtered to images, sorted the same way the file-list
  view sorts (so visual order matches viewer order).
- ← / → cycle prev/next; mouse wheel does the same.
- Path bar updates to the current image's path while in viewer mode.
- *Visible at end of phase*: full keyboard browsing of a folder.

### Phase 3 — pan & zoom

- Add `_zoomLevel`, `_translate`, `_fit` state to `ViewerWidget`.
- Mouse: click-drag pans (only when not fitted); wheel zooms; wheel +
  Ctrl as alternate zoom binding (so plain wheel can stay as
  prev/next — TBD by feel).
- Keyboard: `+` / `-` zoom in/out at the cursor (or center if no cursor),
  `0` / `*` toggle fit, `1` jump to 1:1.
- Pan limits: clamp `_translate` so the image edge can't be dragged past
  the centerline.
- *Visible at end of phase*: usable single-image inspection.

### Phase 4 — progressive loading + thumbnail placeholder

- Show the cached `QImage` thumbnail (from `FileModel::_thumbnails`)
  immediately on activate, scaled to fit. This is the placeholder while
  the full-res streams.
- `ImageLoader` worker on its own QThread: chunked file read into
  `QByteArray`, then decode. Once decoded, emits the full `QImage`. For
  *truly* progressive painting, optionally split the decoded image into
  horizontal slices and emit each slice with its rect (paint slices into
  `_full` as they arrive, repaint after each).
- Cancellation: if the user navigates to the next image before the
  current one finishes, the in-flight load is abandoned (atomic-flag
  pattern from the thumbnail worker).
- *Visible at end of phase*: instant thumbnail-quality preview, real
  image fills in over time on slow shares.

### Phase 5 — fullscreen, rotate, copy-to

- F11 toggles `MainWindow::showFullScreen()` / `showNormal()`. Hide /
  restore menu / status / dock / path edit — remember visibility state
  so we don't unhide things the user explicitly closed.
- Rotate left / right shortcuts (Ctrl+L / Ctrl+R) — applied as a
  per-image rotation that resets between images. Doesn't modify the
  file.
- Context menu on the viewer: **Copy to...** opens a folder picker
  (we already use `QFileDialog::getExistingDirectory` for this on the
  Pixie side). Last-used path remembered in `QSettings`.
- *Visible at end of phase*: feature-parity with Pixie's `ViewWindow`,
  inside one QMainWindow.

## Open questions (decide as we go)

- **Thumbnail placeholder upscale style** — when Phase 4 stretches a
  256×256 thumbnail to fill the screen, do we want nearest-neighbor
  (matches our pixel-art thumbnail upscale logic) or smooth (looks less
  alarming for photos)? Probably smooth — placeholder is meant to fade
  into the real image.
- **Wheel binding** — wheel to navigate (Pixie default) vs wheel to zoom
  (most photo viewers). User to decide on first feel.
- **What does "Enter" do in viewer mode** — close, or zoom-toggle? Pixie
  used Enter to close. Stick with that.

## Future features (not part of this plan)

- Slideshow mode.
- Duplicate-image finder.
- Batch rename / move / delete.
- Multi-tab viewing.
