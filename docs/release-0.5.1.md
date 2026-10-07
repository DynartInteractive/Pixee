# Pixee 0.5.1

A bug-fix release. A full code review turned up several ways for a file
operation to lose files permanently; this release closes all of them, along with
two crashes and a set of smaller viewer, thumbnail and dialog problems. The
complete list of findings and what was done about each is in
[`review.md`](https://github.com/DynartInteractive/Pixee/blob/v0.5.1/review.md).

## Overwrite no longer deletes the file you meant to keep

Every *Overwrite* used to delete the existing file first and only then write the
new one. Two things went wrong with that.

**The "existing" file was sometimes the source itself**, and was deleted before
it could be read:

- *Move to* the folder an item already lives in — the conflict prompt showed the
  file against itself, and Overwrite deleted it.
- A **case-only rename** on Windows (`IMG_0001.jpg` → `img_0001.jpg`, easily
  made with Batch rename's find & replace) — the same.
- **Save As** with the dialog's defaults, whose suggested target *is* the open
  file.
- **File → Save** on a format Qt can open but not write, such as GIF: the
  original was deleted, the write failed, and the edit was already marked saved.

All four are caught before any prompt now. A move into the item's own folder is
refused with a note, a case-only rename just renames, Save As onto the source
re-encodes it safely, and Save on an unwritable format points you at Save As.

**And a failed write left nothing.** Copies and image saves are now written to a
temporary file and swapped in only once complete; moves and renames set the old
file aside and put it back if they fail. A cross-volume move checks that the
copy is whole before removing the source.

## Delete asks before going permanent

On a drive without a Recycle Bin — most network shares — *Delete* used to fall
through to a permanent delete without saying so. It now stops with a message;
**Shift+Delete** remains the explicit permanent delete.

## Your edits are safe until they're on disk

- Closing the window, or clicking a folder in the tree while viewing an edited
  image, now asks Save / Discard / Cancel instead of dropping the edit.
- Quitting while copies or moves are running asks first.
- An edit — including a colour adjustment — is marked saved only once the write
  succeeds, and going back to the image afterwards shows the saved version.

## Also in this release

- **Two crashes fixed:** pressing Abort on a task group that finishes while the
  confirmation is open, and browsing a folder holding a malformed JPEG.
- **Thumbnails** keep filling in after scrolling back and forth over large
  files, memory is released when you leave a folder, and a thumbnail that failed
  on a still-being-written file is retried once the file changes.
- **The viewer** opens images over 64 megapixels, says so when a file can't be
  opened, and steps one image per wheel notch on high-resolution wheels and
  touchpads.
- **Name checks:** Rename, New folder, Batch rename and Save As reject names
  Windows would refuse or silently change (`CON`, a trailing dot or space, `:`).
- **Drag and drop** between two Pixee windows no longer lets the source delete
  files the destination is still moving; dragging over the folder tree expands
  collapsed folders and auto-scrolls.

## Windows notes

Both downloads are **unsigned**, so SmartScreen warns on first run
(*More info → Run anyway*). The installer registers Pixee in Explorer's
*Open with…*; the portable is a self-contained folder — unzip and run
`Pixee.exe`.

## Linux

Build it as a Flatpak from `packaging/net.dynart.Pixee.yml`, or `make install`
under a prefix. On Linux, folder listings now read file sizes and dates off the
UI thread, which keeps network mounts responsive.
