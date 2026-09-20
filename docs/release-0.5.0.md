# Pixee 0.5.0

Keyboard-driven navigation in the path bar, an editable *Open with* list, and a
dark theme that finally covers the parts Qt had been leaving to the operating
system — which also fixes a menu bar that was unreadable on Windows 11.

This is the first Windows build since 0.3.0: 0.4.0 was a Linux packaging
release with no Windows binaries, so everything it and this release changed
arrives together. The full feature and keyboard list lives in the
[README](https://github.com/DynartInteractive/Pixee/blob/v0.5.0/README.md).

## Path bar: type a few letters, not the whole path

The path bar accepted a typed path and navigated on Enter, but offered no help
getting there — you had to know and spell the whole thing, or give up and click
through the folder tree.

Typing now drops down the sub-folders of the directory left of the caret, listed
as full paths:

- **Up / Down** walk the list, and row 0 starts selected, so the common
  type-a-few-letters-then-Enter case needs no arrowing at all.
- **Enter** descends into the highlighted folder and immediately offers *its*
  children, so a deep tree is walkable from the keyboard alone.
- **Esc** closes the list; Enter then navigates exactly as it always did.

The directory listing is read on its own thread and cached per folder, so a
folder is read once per path level rather than once per keystroke — typing stays
responsive even on a slow network share.

## Open with: an Edit button

The configured programs could only be added and removed, so a typo in a label,
or a path that moved after an application upgrade, meant deleting the entry and
re-adding it from scratch. Add and Edit now share one dialog — label, executable
path with a Browse picker, Save disabled until both are usable with the reason
shown inline.

A bare command name is accepted on purpose: it is resolved against `PATH`, which
is how you would configure `gimp` on Linux.

## The dark theme now covers menus and dialogs

Menus, dialogs, message boxes, buttons, text fields, sliders, lists, tables and
tooltips were never named in the theme's stylesheet, so the native platform
style painted them from the system palette. Inside a dark app that meant white
dialogs — untidy, but readable, which is why it went unnoticed.

**On Windows 11 it was worse than untidy.** The Windows 11 style paints no menu
bar background whatsoever, so what showed through was Pixee's own dark window,
while the labels were drawn in the system palette's black:

> Menu bar: black text on `#333`. Effectively unreadable.

Windows 10 never showed it, because its style fills the bar with a light system
colour that black text reads fine on. All of those widgets are themed explicitly
now, so Pixee looks the same whatever Windows is set to — light mode, dark mode,
10 or 11.

## Also in this release

- **Shift+Delete permanently deletes again**, on the file list and in the
  viewer. `QShortcut`'s `StandardKey` constructor also claims Shift+Del for Cut,
  and the collision silently disabled *both* shortcuts — no error, and plain
  Delete kept working, so it read as never having been wired up.
- **The `.exe` and the installer carry the app icon.** Previously only the
  window did, so Pixee was a blank icon in Explorer and in the setup wizard.
- **About reports its environment** — build timestamp, Qt version, the platform
  style that is drawing the UI, and the OS. A screenshot of that box now
  explains why the same build can look different on two machines.
- **The Folders dock title is translatable.** It was the one dock title that
  wasn't.

## Windows notes

Both downloads are **unsigned**, so SmartScreen warns on first run
(*More info → Run anyway*). The installer registers Pixee in Explorer's
*Open with…*; the portable is a self-contained folder — unzip and run
`Pixee.exe`.

Both carry the HEIC / AVIF / JPEG XL / PSD / XCF plugins and the Exiv2 metadata
backend, so the Metadata panel shows full EXIF / IPTC / XMP.

## Linux

Unchanged from 0.4.0: build it as a Flatpak from
`packaging/net.dynart.Pixee.yml`, or `make install` under a prefix. See
[`docs/flatpak.md`](https://github.com/DynartInteractive/Pixee/blob/v0.5.0/docs/flatpak.md).
Pixee is not on Flathub yet.
