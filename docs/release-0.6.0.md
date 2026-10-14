# Pixee 0.6.0

A feature release: a proper Settings window, the first iteration of a plugin
system, and a handful of fixes found while using Pixee day to day. The
Windows data folder also moves to a standard location.

## Settings

`Edit → Settings…` (`Ctrl+,`) now has four pages, with a search box that
filters every label and an icon list on the left.

- **General** — language, theme (any folder under `themes/`, or Qt's own look),
  and where Pixee starts: the last folder, your home folder, the drive list, or
  a folder you pick.
- **Thumbnails** — how many thumbnails decode in parallel (two suits a network
  share; a local disk can take more) and the cell size, 128, 192 or 256 px,
  drawn from the same cached thumbnails so a denser grid costs no re-decode.
- **Viewer** — whether the plain mouse wheel flips images or zooms (`Ctrl` +
  wheel does the other), and whether an image opens fitted only when larger
  than the window, always fitted, or at 1:1.
- **File operations** — ask before deleting to the recycle bin (permanent
  delete always asks), and the JPEG / WebP quality `File → Save` writes with.

Language, theme and the decoder count are read at startup; Save says so in one
notice when one of them changed. Everything else applies as soon as you press
Save.

## Plugins

A plugin is a folder with a `plugin.json` and a JavaScript file, dropped at
`<data dir>/plugins/<name>/`. Whatever it registers shows up under a new
**Plugins** menu, which stays hidden when you have none. This first iteration
gives a plugin the current selection, the current folder and a log; async HTTP,
dialogs and progress-tracked jobs come next. Each plugin runs in its own engine,
so one cannot interfere with another, and a broken plugin is skipped with an
explanation instead of stopping Pixee from starting. See
[`docs/plugins.md`](https://github.com/DynartInteractive/Pixee/blob/v0.6.0/docs/plugins.md).

## Fixes

- **Images in hidden folders open.** An image passed on the command line from
  under `AppData`, or a hidden path typed into the path bar, used to stop at the
  last visible folder and show the drive list. The hidden folder on the way is
  now added to the tree, the way Explorer shows one you navigate into by name.
- **The crop bar can be moved.** It could sit on top of a marquee handle; drag
  it by the dotted grip at its left end. The position lasts for that crop only.
- **The path bar is level with the Folders title**, one pixel taller than
  before.

## Windows notes

The data folder is now `C:\Users\<you>\AppData\Local\Dynart\Pixee` instead of
`~/.pixee`. Your thumbnail cache is carried across on first run, and a theme
left in the old folder keeps working.

Both downloads are **unsigned**, so SmartScreen warns on first run
(*More info → Run anyway*). The installer registers Pixee in Explorer's
*Open with…*; the portable is a self-contained folder — unzip and run
`Pixee.exe`.

## Linux and macOS

No prebuilt binaries yet, but Pixee is tested on both. Build it as a Flatpak
from `packaging/net.dynart.Pixee.yml`, `make install` under a prefix, or
`qmake && make` on a Mac with Qt 6.6 or later.
