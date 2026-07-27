# Prebuilt extra image-format plugins (HEIC / AVIF / PSD / XCF)

These are **prebuilt binaries** kept here so `scripts\make-portable.bat` can bundle
HEIC/AVIF (and PSD/XCF) support into a portable Pixee without needing vcpkg or a
live build tree. They were rescued from the last working MSVC build after vcpkg
(their original source) was removed from the machine.

## Contents

- `plugins\kimg_*.dll` — KDE **kimageformats** Qt plugins. Copied into
  `Pixee-portable\imageformats\`. These link **Qt 6.11.1 MSVC** and only load
  into an **MSVC-built** Pixee — a MinGW build can't use them.
- `codecs\*.dll` — the runtime codec libraries the plugins call into
  (`heif/libde265/libx265/avif/dav1d/aom/libyuv`, plus `jpeg62` which
  `libyuv` links — omit it and `avif.dll` fails to load with a "module not
  found" and AVIF silently drops out of the format list). Plain C libs (no Qt
  dep). Copied **next to `Pixee.exe`** — the app dir is on the plugin
  dependency search path (`LOAD_LIBRARY_SEARCH_APPLICATION_DIR`).

## Provenance

Built once against Qt 6.11.1 / MSVC 2022 per `docs\windows-extra-image-formats.md`:

- `kimg_*.dll` — from `github/kde/kimageformats` (repo checkout at
  `C:\Users\gopher\Projects\kimageformats`), CMake build with
  `-DKIMAGEFORMATS_HEIF=ON`.
- `codecs\*.dll` — from vcpkg (`x64-windows` triplet): `libheif libde265`
  `libavif[dav1d,aom] dav1d aom` (libyuv/libx265 pulled in transitively).

## Rebuilding (only if Qt is upgraded or a DLL is lost)

Follow `docs\windows-extra-image-formats.md` Stages 2–3, then refresh this folder:

- copy the new `kimg_heif/avif/psd/xcf.dll` into `plugins\`
- copy the codec DLLs from `<vcpkg>\installed\x64-windows\bin\` into `codecs\`

vcpkg is **not** needed to *use* these files — only to regenerate them.
