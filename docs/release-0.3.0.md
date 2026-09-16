# Pixee 0.3.0

Colour adjustment and a live histogram. Two new docks join Metadata in the
right-hand column, and the viewer grows a proper non-destructive editing layer
on top of the rotate / flip / crop it already had.

Everything from 0.1.0 and 0.2.0 is still here — this page covers what changed.
The full feature and keyboard list lives in the [README](../README.md).

## Colour adjustment

**Adjust dock** (`View → Adjust`, or `Edit ▸ Adjust colours…` in the viewer) —
live sliders for **brightness, contrast, saturation, hue and gamma**, applied to
the image as you drag. Hue runs ±180°, gamma 0.25–4.0, the rest ±100.

**These are not baked pixels.** Rotate, flip and crop bake — they are one-shot
transforms. A slider is not: re-applying "+5 brightness" to an already
brightened image on every mouse-move would compound and quietly destroy detail,
and dragging back to zero would leave you somewhere other than where you
started. So Pixee keeps the adjustment as a *description* and recomputes from
the original pixels every time it changes. Three things fall out of that:

- Dragging a slider back to zero restores the original **exactly**.
- Adjustments **survive a rotation** — rotate an adjusted image and the settings
  stay put, because they are re-applied to the rotated pixels rather than being
  stuck in them.
- Hold **`B`** to see the unadjusted image for as long as you hold it.

**Resetting** — double-click any slider's label to reset just that row, or
**Reset all** for the lot.

**It stays responsive on big files.** The preview runs on a screen-resolution
proxy rather than the full image: a full pass over a 24 MP photo measures
174 ms, which is far too slow to drag against; the proxy does it in 15 ms.
Zooming past 1:1 falls back to full resolution, so inspecting detail
mid-adjustment stays sharp rather than going soft. The full-resolution pass
happens exactly once — when you save.

**Saving** — an adjustment counts as an unsaved edit, so `File → Save`
(`Ctrl + S`), `Save As…` (`Ctrl + Shift + S`) and the prompt when you navigate
away all cover it, the same as a crop or a rotation.

The panel is **viewer-only** and greys out while you are browsing, since
adjusting needs the full-resolution image.

## Histogram

**Histogram dock** (`View → Histogram`) — **RGB** (additively blended, so
overlapping channels read as the colour they actually make) or **luminance**, an
optional **log scale** for when one flat expanse of sky would otherwise squash
everything else onto the axis, and a **clipping readout** giving the percentage
of the image pushed off each end.

**In the viewer** it reads the *same* pixels being painted, so it follows every
adjustment as you drag rather than lagging a step behind — watch the clipping
figures while you push contrast. It always covers the whole image, so panning
and zooming never reshape it.

**While browsing** it follows the file list instead: select a single image and
it reads that file off-thread, so a slow network share never stalls the list.
Select several, or none, and the graph empties rather than leaving the previous
image's distribution on screen looking like it belongs to the new selection.

## Interface

**The right-hand docks stack.** Metadata, Adjust and Histogram now sit above one
another rather than as tabs, so an open histogram and an open metadata panel are
visible at the same time — which matters when the histogram is the thing you are
watching while you drag a slider. All three fit alongside the image at
1280×720, Pixee's baseline resolution.

**Dark theme** — the new panels' labels, spin boxes and drop-downs are themed
properly rather than falling back to near-black text on a dark background.

## File operations

**`Del` / `Shift + Del` with the folder tree focused** deletes the folder
selected there — to the recycle bin or permanently — with the same confirmation
and task pipeline as the file list, then steps the selection up to the parent.

**`Ctrl + V` with the folder tree focused** pastes into the folder selected
there, rather than into the folder you happen to be browsing.

## Fixed

**Folders open scrolled to the top again.** Qt re-roots a list view without
resetting its scroll offset, so a folder could open part-way down — or pinned to
its bottom when it was shorter than the one before it.

## Getting it

- **Installer** — `Pixee-0.3.0-setup.exe`. Bundles everything: the Qt runtime,
  the VC++ runtime, HEIC / AVIF / PSD / XCF support, and the Exiv2 backend for
  full EXIF / IPTC / XMP in the Metadata panel.
- **Portable** — `Pixee-0.3.0-portable.zip`. Same contents, unzip and run, no
  installation needed. It still keeps its thumbnail cache in `~/.pixee` and its
  window/panel state in the registry, so it is not a no-trace build.

The installer is still **unsigned**, so SmartScreen will warn on first run —
choose *More info → Run anyway*.

## Notes

Adjustments live for as long as you have the image open. Pixee does not keep
them in a sidecar file to re-apply later; save the result if you want to keep
it. Rotation still re-encodes the pixels — lossless orientation-only rotation
needs metadata *write* support, which the Exiv2 backend does not do yet.
