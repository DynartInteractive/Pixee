# Image metadata panel (Exiv2)

Pixee has a read-only **Metadata** panel (View → Metadata, a right-side dock)
that shows the focused image's info: dimensions, format, size — and, when the
Exiv2 backend is compiled in, full **EXIF / IPTC / XMP** (camera, exposure,
date taken, GPS, and a complete tag dump).

The panel works in **two builds**:

- **Default build (no Exiv2):** shows the Qt-derived basics only. Nothing to
  install; the panel just omits the camera/exposure/location groups and notes
  that EXIF isn't available in this build.
- **Exiv2 build:** the full metadata. This needs the Exiv2 library present at
  build time and its DLL bundled at run time (Windows/MSVC).

Everything below is about turning on the Exiv2 build.

## Architecture (why it's structured this way)

- `MetadataReader` (`src/MetadataReader.cpp`) runs on its **own thread** and
  reads off the GUI thread — same supersede-on-navigate pattern as
  `ImageLoader`, so a slow read on an SMB share never blocks prev/next.
- It converts Exiv2's data into a plain `ImageMetadata` struct
  (`src/ImageMetadata.h`) that crosses the queued signal. **Exiv2 headers never
  leave `MetadataReader.cpp`** — the rest of the app has no Exiv2 dependency.
- The whole Exiv2 parse is wrapped in `#ifdef PIXEE_HAVE_EXIV2`. Without the
  define it compiles to a Qt-only stub; with it, the real parse runs.

## ⚠️ Licence — Exiv2 is GPLv2+

Exiv2 is licensed **GPLv2-or-later**. Pixee itself is MIT, but a binary that
**links Exiv2 is a combined work** and must be distributed under GPL-compatible
terms. Because Pixee's source is already public on GitHub, compliance is
straightforward, but be aware:

- Ship an Exiv2 **licence/attribution** with the Exiv2-enabled binaries
  (installer `licenses/` folder + a credit in Help → About).
- The default (no-Exiv2) build is unaffected — pure MIT.

Decision on record (see the project memory): **GPL accepted** for the
Exiv2-enabled binaries.

## 1. Get the prebuilt Exiv2 (MSVC x64)

Download the Windows MSVC build from the Exiv2 releases:
<https://github.com/Exiv2/exiv2/releases> — pick the **`2022msvc-AMD64`** asset
(MSVC 2022, x64), matching Pixee's `msvc2022_64` kit. As of this writing the
current release is **v0.28.8**:

<https://github.com/Exiv2/exiv2/releases/download/v0.28.8/exiv2-0.28.8-2022msvc-AMD64.zip>

Don't grab the `Linux`/`Darwin` tarballs, a MinGW build, or a 32-bit one.

> MinGW note: like the extra image-format plugins, the MSVC Exiv2 build won't
> link into a MinGW Pixee. The shipped portable/installer is MSVC, so this is
> fine — just don't try to enable Exiv2 on a MinGW kit.

## 2. Lay the files into `thirdparty/exiv2/`

Create this layout (mirrors `thirdparty/imageformats/`):

```
thirdparty/exiv2/
├── include/
│   └── exiv2/            # all Exiv2 headers (exiv2.hpp, ...)
├── lib/
│   └── exiv2.lib         # the MSVC import library
└── bin/
    ├── exiv2.dll         # the runtime DLL
    └── *.dll             # any dependency DLLs shipped in the zip's bin/
                          # (e.g. expat, zlib, brotli — copy whatever is there)
```

Copy from the unzipped release: its `include/` → `include/`, its
`lib/exiv2.lib` → `lib/`, and **everything** in its `bin/*.dll` → `bin/`.

`thirdparty/exiv2/` is git-ignored (binary blobs), same as the imageformats
DLLs — keep it local / in your release toolbox.

## 3. Build with the backend on

Pass `PIXEE_HAVE_EXIV2=1` to qmake (from an x64 Native Tools Command Prompt):

```cmd
qmake PIXEE_HAVE_EXIV2=1 Pixee.pro
nmake
```

qmake prints `Exiv2 metadata backend: ENABLED` when the flag is picked up. The
guarded block in `Pixee.pro` adds the include path, links `exiv2.lib`, and
defines `PIXEE_HAVE_EXIV2`.

## 4. Bundle the DLLs in the portable

For the portable/installer to run on a clean machine, `exiv2.dll` (+ its
dependency DLLs) must sit next to `Pixee.exe`. Add a copy step to
`scripts/make-portable.bat` alongside the kimg-codec copy, e.g.:

```bat
REM ---- Exiv2 metadata backend (MSVC builds with PIXEE_HAVE_EXIV2) ----------
if exist "%~dp0..\thirdparty\exiv2\bin\exiv2.dll" (
    copy /Y "%~dp0..\thirdparty\exiv2\bin\*.dll" "%OUT_DIR%\" >nul
    echo   Bundled Exiv2 runtime DLLs.
)
```

Then add the Exiv2 licence file to the installer's `licenses/` and a credit in
the About dialog.

## Verifying

- Build with `PIXEE_HAVE_EXIV2=1`, open a JPEG shot on a phone → the panel
  should show Make/Model, exposure triple, and GPS if present.
- A PNG with text chunks → those show under "All metadata".
- A BMP → basics only (BMP carries no EXIF), which is expected.

## References

- Exiv2: <https://exiv2.org/> · API: <https://exiv2.org/doc/>
- `docs/windows-extra-image-formats.md` — the parallel recipe for HEIC/AVIF/PSD
  plugins; the thirdparty/bundling conventions match.
