# Pixee vs XnView MP

An honest comparison for deciding what Pixee should do next, not a marketing
page. Written against **Pixee 0.6.0** and **XnView MP 1.12.x** (the latest
release at the time of writing, 2026-10). The XnView MP side comes from the
program's public documentation, its forum and years of use by many people; it
has hundreds of options and this file does not try to list them all. Where a
detail could not be confirmed it says so.

## The two programs in one paragraph each

**XnView MP** is the cross-platform rewrite of XnView, a freeware viewer that
has been around since 1998. It is a *do-everything* photo organiser: browser
with tabs and a database of categories and ratings, viewer with compare and
slideshow modes, an editor with levels, curves, filters and effects, a batch
converter with dozens of operations, a batch renamer, metadata editing, a
duplicate finder, printing, screen capture and more. It reads around 500 image
formats through the author's own decoding library, including camera RAW, and
writes about 70. It is closed source, free for private, educational and
non-profit use, and a company licence costs €29 per seat. Windows, macOS and
Linux builds are provided. The current line is 1.12.

**Pixee** is a deliberately small image manager built on Qt 6: a folder browser
with a fast thumbnail grid, a viewer with rotate / flip / crop and parametric
colour adjustment, a metadata panel, file operations with a careful conflict
prompt, batch rename, and JavaScript plugins. It reads what Qt's image plugins
decode. It is MIT-licensed, has prebuilt Windows binaries (installer and
portable, ~55 MB), builds on Linux and macOS, and is on its way to Flathub.

## At a glance

| | XnView MP | Pixee 0.6.0 |
|---|---|---|
| First release | 2007 (XnView 1998) | 2024 |
| Licence | Proprietary freeware; €29/seat for companies | MIT, open source |
| Platforms with binaries | Windows, macOS, Linux | Windows (installer, portable); Linux / macOS from source, Flatpak manifest in progress |
| Toolkit | Qt 5/6 (closed app) | Qt 6 |
| Install size | roughly 150–200 MB unpacked | ~130 MB unpacked, 55 MB installer |
| Read formats | ~500 incl. RAW, PSD, HEIC, AVIF, JXL, video frames | Qt plugins: JPEG, PNG, WebP, GIF, BMP, ICO, TIFF, SVG; HEIC / AVIF / PSD / XCF with kimageformats |
| Write formats | ~70 | what the Qt build can write (JPEG, PNG, WebP, BMP, TIFF, …) |
| Interface | dense, every feature exposed, many layouts | minimal, one grid, one viewer, three docks |
| Languages | 40+ | English; four others scaffolded, untranslated |
| Extensibility | none for users (command line and the NConvert CLI sibling) | JavaScript plugins |

## Where XnView MP is clearly ahead

These are real gaps. Some matter for Pixee's goals, some do not.

- **Format breadth.** RAW from every camera brand, PSD, EXR, HDR, JXL, DDS,
  video thumbnails via FFmpeg, PDF pages. Pixee gets the common formats from
  Qt and the rest only when kimageformats is built against its Qt, which the
  Windows build does ship, but RAW and video are missing entirely.
- **Organising.** A database of categories (hierarchical keywords), ratings
  (0–5 stars), colour labels, a filter bar, a search dialog that queries
  filename, metadata and categories, and favourites. Pixee has none of this:
  the folder is the only organising unit.
- **Browser features.** Tabs with saved sessions, a filmstrip, several thumbnail
  layouts with configurable labels under each thumbnail, a *compare* mode
  showing up to four images side by side with linked zoom, a *find
  duplicates / similar* tool, a slideshow with transitions.
- **Metadata editing.** IPTC and XMP fields can be written in a dedicated
  editor, EXIF orientation can be fixed, GPS positions shown on a map.
  Pixee reads metadata only.
- **Editing depth.** Levels, curves, auto-contrast, white balance, shadows /
  highlights, resize with several resamplers, canvas size, red-eye, a set of
  filters and effects (blur, sharpen, emboss, …), text and shape overlays.
  Pixee has rotate, flip, crop and five global sliders.
- **Lossless JPEG transforms.** Rotation and crop of a JPEG without
  re-encoding. Pixee re-encodes on rotate (planned with metadata write
  support, see README).
- **Batch convert.** Dozens of chainable operations (resize, watermark,
  colour, rename, format) applied to a selection or a folder, with saved
  scripts. Pixee has batch *rename* only; a single file can be converted
  through Save As.
- **Colour management.** ICC profiles embedded in the file and a monitor
  profile are honoured. Qt's image loaders do not apply embedded profiles by
  default, so a wide-gamut JPEG shows slightly wrong in Pixee.
- **Everything else.** Printing, contact sheets, screen capture, set as
  wallpaper, image to PDF, hex view, a colour picker, a zoom lens, a
  thumbnail cache size management dialog, and so on.

## Where Pixee is ahead

Fewer items, but they are the ones the project is built around.

- **Speed on big and remote folders.** Pixee's whole file model is written
  for network shares: directory listing off the GUI thread, chunked reads
  with cooperative abort, a viewport-ordered decode queue, negative caching,
  and no QFileSystemModel. XnView MP is a fast program on a local disk, but
  its browser can stall on a large SMB folder, and its options for
  thumbnails are tied to its own cache database.
- **Focus.** A new user sees a grid, a viewer and three optional panels.
  XnView MP's Settings dialog has around twenty pages, and the main window
  has toolbars, a category pane, an info pane, a preview pane and a filmstrip
  that each need configuring or hiding. Pixee's density rule (everything
  must fit at 1280×720) keeps the UI readable on a laptop.
- **Non-destructive adjustment.** Pixee stores brightness / contrast /
  saturation / hue / gamma as parameters, previews on a proxy, and applies
  them once at full resolution on save. A slider never compounds, the
  original is one `B` keypress away, and the histogram follows every drag
  live. In XnView MP each Adjust dialog bakes pixels; undo is a separate
  step and the histogram is a static panel.
- **Crop.** Fixed-ratio crop with eight handles, a draggable interior, a
  pixel readout and a movable ratio bar is in the viewer itself, not in a
  modal editor. XnView MP's crop is a selection rectangle plus a menu action.
- **AI-generated image metadata.** Pixee shows ComfyUI and Automatic1111
  generation data from PNG text chunks out of the box, with copy-value on
  every row. XnView MP shows PNG text chunks in its raw info tab but does
  not name or format them.
- **File operations that explain themselves.** The side-by-side conflict
  prompt (Existing vs Incoming, each with a thumbnail, size and date), moves
  that keep the cached thumbnail, overwrite that never loses the old file
  on failure, delete that refuses instead of deleting permanently on a
  share without a recycle bin. XnView MP uses the OS copy dialogs with a
  plain *Overwrite? Yes / No / All* prompt.
- **Folder index thumbnails.** A folder shows its first image on the folder
  icon. XnView MP shows a plain folder icon (it can show the first image in
  some layouts, but not on the folder tile itself).
- **Path bar completion** that walks a deep tree on the keyboard with one
  directory read per level.
- **Plugins.** A JavaScript plugin can add menu entries and read the
  selection today, with HTTP and dialogs planned. XnView MP has no user
  scripting at all beyond the command line.
- **Open source.** Anyone can fix a bug, build it for an unusual platform,
  or audit what it does with their files. XnView MP's bug tracker is the
  author's forum, and the decoding library is closed.
- **Theming.** A theme is a folder with a Qt stylesheet and an INI; users
  can override it without a rebuild. XnView MP has a light / dark switch
  and icon sets.

## Head-to-head by area

| Area | XnView MP | Pixee | Note |
|---|---|---|---|
| Thumbnail grid | ✔ many layouts, labels, overlays | ✔ one layout, three cell sizes | |
| Thumbnail cache | ✔ SQLite | ✔ SQLite, WAL, format auto-detect | |
| Folder tree | ✔ | ✔ hides in viewer | |
| Tabs / sessions | ✔ | ✘ | |
| Sorting | ✔ many keys | ✔ name / created / modified | |
| Filter / search | ✔ | ✘ | |
| Ratings / labels / categories | ✔ database | ✘ | |
| Favourites | ✔ | ✘ | |
| Path bar with completion | partial (editable combo) | ✔ walks subfolders | |
| Viewer | ✔ | ✔ | |
| Fullscreen | ✔ | ✔ `F11` | |
| Preload next / previous | ✔ | ✔ five images | |
| Compare side by side | ✔ up to four | ✘ | |
| Slideshow | ✔ | ✘ | |
| Zoom modes | ✔ | ✔ fit / 1:1 / 0.1×–8× | |
| Pixel-art upscaling | ✔ option | ✔ automatic below cell size | |
| Rotate / flip | ✔ lossless for JPEG | ✔ re-encodes | |
| Crop | ✔ | ✔ handles, fixed ratio, readout | Pixee's is the nicer tool |
| Resize / canvas | ✔ | ✘ | |
| Brightness / contrast / gamma / hue / sat | ✔ baked | ✔ parametric, live | |
| Levels / curves / white balance | ✔ | ✘ | |
| Filters / effects | ✔ | ✘ | |
| Histogram | ✔ static panel | ✔ live, RGB / luma, log, clipping | |
| Metadata read | ✔ EXIF / IPTC / XMP / ICC | ✔ EXIF / IPTC / XMP with Exiv2; PNG text | |
| Metadata write | ✔ | ✘ | |
| GPS map | ✔ | ✘ | |
| AI generation data | raw text chunk | ✔ named, copyable | |
| Copy / move / rename / delete | ✔ OS dialogs | ✔ own task pipeline, side-by-side conflicts | |
| Batch rename | ✔ | ✔ pattern, `{n:3}`, live preview | comparable |
| Batch convert | ✔ | ✘ | |
| Save As / convert one file | ✔ | ✔ with quality slider | |
| Duplicate finder | ✔ | ✘ | |
| Print / contact sheet | ✔ | ✘ | |
| Open with external editor | ✔ | ✔ configurable list | |
| Drag and drop to other apps | ✔ | ✔ as a plain file list | fixed in 0.6.x |
| Colour management | ✔ | ✘ | |
| Formats | ~500 | Qt + kimageformats | |
| RAW | ✔ | ✘ | |
| Video thumbnails | ✔ | ✘ | |
| Plugins / scripting | ✘ | ✔ JavaScript | |
| Themes | light / dark | ✔ stylesheet folders | |
| Translations | 40+ | English only in practice | |
| macOS binaries | ✔ | ✘ | |
| Open source | ✘ | ✔ MIT | |

## What this means for Pixee

XnView MP cannot be beaten on feature count, and trying would throw away the
focus that makes Pixee pleasant. The useful question is which XnView MP
features a *photo folder* user misses within the first hour, and which Pixee
strengths deserve to be louder.

Gaps that a first-hour user hits, roughly ordered by effort:

1. **Filter by name** in the current folder (a text box that hides
   non-matching cells). Small; the proxy model already exists.
2. **Slideshow** (fullscreen viewer plus a timer and a wrap option). Small.
3. **Lossless JPEG rotate** and EXIF orientation write. Medium; comes with
   the planned metadata write support.
4. **Colour management** for embedded ICC profiles. Medium; Qt has
   `QColorSpace` and `QImage::convertToColorSpace`, so it is mostly wiring
   plus a monitor-profile setting.
5. **Resize on Save As** (long edge or percentage). Small to medium; it fits
   the existing Save As dialog.
6. **Ratings** stored as XMP `xmp:Rating` in the file, so they travel to
   other tools. Medium; needs metadata write.
7. **RAW** through LibRaw as an optional backend, like Exiv2 today. Medium;
   Windows packaging is the main cost.
8. **Compare two images** side by side. Medium.
9. **Translations** for the four scaffolded languages. Small per language,
   but someone has to write them.
10. **macOS binaries.** Packaging work, no code.

Strengths worth stating on the website and in the README, because XnView MP
users will not expect them:

- Fast and responsive on network shares and huge folders.
- Edits that never compound and a histogram that moves with the slider.
- AI image metadata readable at a glance.
- Conflict prompts you can decide by eye.
- Small, readable UI that fits a 1280×720 laptop screen.
- Open source and scriptable.

## Sources

- [XnView on Wikipedia](https://en.wikipedia.org/wiki/XnView) (version
  1.12.1, 29 September 2026)
- [XnView MP product page](https://www.xnview.com/en/xnview-mp/) (licence
  terms and €29 price)
- [What's different in XnView MP, forum feature list](https://newsgroup.xnview.com/viewtopic.php?p=157716)
- [Neowin changelog for 1.10.4](https://www.neowin.net/amp/xnview-mp-1104/)
- Pixee's own `README.md` and `CHANGELOG.md`
