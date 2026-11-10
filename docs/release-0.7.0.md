# Pixee 0.7.0

A feature release built around day-to-day browsing: single-key panel toggles,
a name filter for the folder, Copy path, and the side panels remembering how
you had them. Plus a set of fixes, one of which fixes a Windows oddity you
may have noticed outside Pixee.

## Keyboard

- **One key per panel.** `F`, `M`, `A`, `H` and `T` show or hide the Folders,
  Metadata, Adjust, Histogram and Tasks panels, while browsing and in the
  viewer alike; the `View` menu shows the letters. To make room, the viewer's
  **flip horizontal moved from `H` to `Shift + H`** (flip vertical stays on
  `V`).
- **Filter the folder by name with `Ctrl + F`.** A strip opens under the path
  bar; type and the grid keeps only the names that contain the text, folders
  included. `*` and `?` make it a whole-name pattern (`*.png`). The status bar
  counts what is on screen, the viewer walks only the matches, `Esc` clears,
  and entering another folder drops it.
- **Copy path** (`Ctrl + Shift + C`), in the right-click menu of the file list
  and the viewer, puts the full path on the clipboard as plain text with no
  file attached: paste it into a chat, a terminal or a file dialog and you get
  the path, not a copy of the file. A multi-selection copies one path per
  line.

## Browsing and viewing

- **Image position in the viewer.** The right end of the status bar shows
  where you are in the folder (`3 / 10`) while an image is open. It counts the
  images the viewer walks, so a name filter narrows it and a delete or move
  shortens it.
- **The side panels remember separately** whether they are open while browsing
  and while viewing, the way the folder tree already hides in the viewer and
  comes back afterwards. Open the Histogram in the viewer and it stays there
  for every image, but the grid gets its full width back when you leave.
- **See and change the remembered Copy to / Move to folders.** Hovering the
  context menu's `Copy to "…"` / `Move to "…"` item shows the full path, and
  `Settings → File operations` lists both folders in full, where they can be
  pointed elsewhere or cleared.

## Fixes

- **A file operation that failed said nothing.** The task knew why (a Delete
  on a drive without a Recycle Bin, a source that could not be read, a
  destination that could not be written) but its entry left the Tasks panel
  the moment it finished, taking the message with it. A failure now shows the
  reason in a notification, naming the file.
- **Dragging images into Discord and other Chromium-based apps was refused.**
  On Windows a drag from Pixee arrived as a link and as text as well as a file
  list. It now arrives exactly as one from Explorer does.
- **Closing the viewer in a large folder left the file list in the wrong
  place.** The list did scroll to the image you had been looking at, but a
  moment later the grid re-flowed to a different width and took the scroll
  position with it. The list now keeps that image in view through the
  re-flow.
- **The status bar said "Images: 0 | Folders: 0"** for a folder entered for
  the first time, startup included, until something made it recount.
- **The crop bar has square corners**, like everything else in the dark theme.

## Windows notes

**If Pixee is your default image viewer, your Start menu search may have shown
Pixee's icon on unrelated apps** (Windows Terminal, iTunes…), and clearing the
icon cache did not help for long. The installer registered the app icon as the
icon of image files, and Windows reads some of its own icons out of `.png`
files. The installer now registers the stock picture-file icon for image
documents instead; installing 0.7.0 over an older version fixes it. If you use
the portable and chose Pixee through *Open with → Always*, the one-line
registry fix is in
[`docs/installer.md`](https://github.com/DynartInteractive/Pixee/blob/v0.7.0/docs/installer.md).

Both downloads are **unsigned**, so SmartScreen warns on first run
(*More info → Run anyway*). The installer registers Pixee in Explorer's
*Open with…*; the portable is a self-contained folder — unzip and run
`Pixee.exe`.

## Linux and macOS

No prebuilt binaries yet, but Pixee is tested on both. Build it as a Flatpak
from `packaging/net.dynart.Pixee.yml`, `make install` under a prefix, or
`qmake && make` on a Mac with Qt 6.6 or later.
