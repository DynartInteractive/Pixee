# Extra image format plugins on Windows (MSVC)

Pixee uses Qt's image-plugin system; everything `QImageReader::supportedImageFormats()` reports is recognized transparently at startup (`Config::_setUpImageExtensions`). This document covers wiring up the optional plugins that aren't part of stock Qt:

- **WebP, TIFF, TGA, WBMP, ICNS, MNG** — Qt's own *Qt Image Formats* add-on. One click in the Maintenance Tool.
- **HEIC/HEIF, AVIF, PSD, XCF, DDS, QOI, HDR, …** — KDE's [`kimageformats`](https://invent.kde.org/frameworks/kimageformats), which we build from source against the user's Qt plus vcpkg-installed codec libraries.

The end state: `kimg_*.dll` plugins land in `<build>/release/imageformats/`; runtime codec DLLs (`avif.dll`, `heif.dll`, …) sit next to `Pixee.exe` in `<build>/release/`; and the startup log's `Supported image formats:` line includes `avif`, `avifs`, `heif`, `heic`, `psd`, `xcf`, etc.

The recipe below was nailed down in May 2026 against Qt 6.11.1, MSVC 2022 17.8 (toolset 14.38), and the vcpkg ports as of that date. Most of it should age well; the troubleshooting section at the end captures the specific traps we hit.

## Prerequisites

- Qt **6.11.1 or later** with the **MSVC 2022 64-bit** kit (Qt 6.11.0 doesn't expose the Image Formats add-on in the Maintenance Tool — see gotcha 1).
- Visual Studio 2022 (Community or Build Tools) with the C++ workload.
- CDB debugger from the standalone Windows SDK installer if you want a debuggable kit — VS Build Tools alone no longer ships it. Grab https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads and tick *Debugging Tools for Windows* only.
- [vcpkg](https://github.com/microsoft/vcpkg) checked out, with `bootstrap-vcpkg.bat` run. Paths below use `C:\vcpkg` as a placeholder.

Pixee itself builds with both MinGW and MSVC, but the plugin chain assumes MSVC because vcpkg's mainstream `x64-windows` triplet is MSVC. Plugins built for MSVC won't load into a MinGW build, and vice versa — pick one for shipping.

## Stage 1 — Qt Image Formats add-on (the cheap formats)

Run `<Qt-install>\MaintenanceTool.exe` → **Add or remove components** → expand **Qt 6.11.1 → MSVC 2022 64-bit** → tick **Qt Image Formats** under *Additional Libraries*. Installs `qwebp.dll`, `qtiff.dll`, `qtga.dll`, `qwbmp.dll`, `qicns.dll`, `qmng.dll` into Qt's `plugins/imageformats/`.

`windeployqt` picks these up automatically when you deploy Pixee. No further action.

## Stage 2 — vcpkg-installed codec libraries

```cmd
C:\vcpkg\vcpkg.exe install ecm libheif libde265 "libavif[dav1d,aom]" dav1d aom pkgconf --triplet x64-windows
```

Slow first time (~30–60 minutes; vcpkg builds from source, and pulls in `qtbase` as a build dep). Key flags:

- **`libavif[dav1d,aom]`** — the `[features]` syntax is **mandatory**. A bare `libavif` builds an `avif.dll` with no codec wrapper compiled in, and at runtime `avifCodecName()` returns NULL. kimg_avif then silently filters `avif`/`avifs` out of `supportedImageFormats()`. See gotcha 6.
- **`pkgconf`** — kimageformats' CMake uses `pkg_check_modules(LibHeif libheif>=1.10.0)` to find libheif. The vcpkg toolchain file we pass in Stage 3 wires `pkgconf` in as `PKG_CONFIG_EXECUTABLE`; without it HEIF support stays disabled.
- **`ecm`** — KDE's Extra CMake Modules. Required for any kimageformats build.

## Stage 3 — Build `kimageformats` from source

vcpkg has the codec libraries but **not** `kimageformats` itself — the plugin layer we build by hand.

```cmd
cd C:\workspace
git clone https://invent.kde.org/frameworks/kimageformats.git
cd kimageformats
git checkout v6.25.0
```

Pin to the kimageformats tag whose version matches vcpkg's ECM (`vcpkg list ecm`). KDE Frameworks all version together and require ECM ≥ the same number, so `kimageformats` master typically wants a newer ECM than vcpkg has cached.

Configure (all on one line in cmd; backticks below are line continuations for readability):

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 ^
  -DCMAKE_INSTALL_PREFIX=C:/workspace/kimageformats/install ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_TESTING=OFF ^
  -DKIMAGEFORMATS_HEIF=ON
```

Critical flags:

- `-DCMAKE_TOOLCHAIN_FILE=…vcpkg.cmake` — wires up vcpkg's `pkgconf` as `PKG_CONFIG_EXECUTABLE` and adds vcpkg's installed prefix to `CMAKE_PREFIX_PATH`. Don't skip this.
- `-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64` — points CMake at your Qt install. The toolchain file extends this rather than replacing it, so the two coexist.
- `-DKIMAGEFORMATS_HEIF=ON` — HEIF is opt-in, defaults to OFF. Without it, the plugin is silently dropped even when libheif is installed and detected. Other formats (AVIF, PSD, XCF, …) auto-enable based on dependency presence.
- `-DBUILD_TESTING=OFF` — skips Qt6Test-dependent autotests.

Scan the feature summary at the end of configure. You want both `LibHeif` and `libavif` listed under enabled/found packages. If `LibHeif` shows up under "disabled," either `KIMAGEFORMATS_HEIF=ON` wasn't passed or pkgconf isn't on the build path.

Build + install:

```cmd
cmake --build build --config Release --target install
```

Output lands in `C:/workspace/kimageformats/install/lib/plugins/imageformats/` — about 18 `kimg_*.dll` plugins. Build is fast (~30 seconds) since the plugins are thin Qt wrappers over the codec libraries.

## Stage 4 — Deploy into Pixee's build directory

Assuming Pixee's MSVC Release output is at `C:/Pixee/build/Desktop_Qt_6_11_1_MSVC2022_64bit-Release/release/Pixee.exe`. Adjust paths for your kit name.

```cmd
:: 1. windeployqt sets up Qt's DLLs and built-in plugins next to Pixee.exe
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe ^
  C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\Pixee.exe

:: 2. Copy kimageformats plugins next to Qt's plugins
copy /Y C:\workspace\kimageformats\install\lib\plugins\imageformats\kimg_*.dll ^
        C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\imageformats\

:: 3. Copy runtime codec DLLs next to Pixee.exe (NOT into imageformats/)
copy /Y C:\vcpkg\installed\x64-windows\bin\heif.dll      C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\libde265.dll  C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\libx265.dll   C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\avif.dll      C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\dav1d.dll     C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\aom.dll       C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
copy /Y C:\vcpkg\installed\x64-windows\bin\libyuv.dll    C:\Pixee\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release\release\
```

Plugin DLLs live in `imageformats/`. Their runtime codec deps live next to `Pixee.exe` — Qt's plugin loader uses `LoadLibraryEx` with `LOAD_LIBRARY_SEARCH_APPLICATION_DIR`, so the application directory is on the search path for plugin dependencies. Putting the codec DLLs in `imageformats/` works too but is redundant.

Start Pixee. The `Supported image formats:` log line should now look something like:

```
Supported image formats: QList("avif", "avifs", "bmp", "cur", "gif", "heic",
"heif", "icns", "ico", "jfif", "jpeg", "jpg", "pbm", "pdd", "pgm", "png",
"ppm", "psb", "psd", "psdt", "svg", "svgz", "tga", "tif", "tiff", "wbmp",
"webp", "xbm", "xcf", "xpm")
```

## Gotchas (writing these down so the next person doesn't relive them)

1. **Qt 6.11.0 doesn't ship the Image Formats add-on.** It's only published for 6.11.1+. If `qwebp.dll` is missing from your Qt's `plugins/imageformats/` and you can't find the checkbox in the Maintenance Tool, upgrade Qt — there's no separate download path.

2. **VS 2022 Build Tools no longer includes `cdb.exe`.** Even though it bundles a Windows SDK. Get the debugger from the standalone Windows SDK installer (`Debugging Tools for Windows`).

3. **vcpkg has libheif/libde265/libavif/dav1d/aom — but not kimageformats.** Build kimageformats from source as in Stage 3. Don't waste time searching for a port; it's never been added.

4. **kimageformats master expects the newest ECM.** vcpkg's ECM lags by 0–2 minor versions. Check out a kimageformats tag (`v6.25.0` and similar) that matches `vcpkg list ecm`'s version, otherwise CMake will refuse to configure.

5. **`KIMAGEFORMATS_HEIF=ON` is mandatory at configure time.** HEIF is the only plugin behind a CMake opt-in. Other formats (AVIF, PSD, XCF, JXL, …) auto-enable based on dependency presence.

6. **`libavif[dav1d,aom]` features are not the default.** A bare `vcpkg install libavif` builds an `avif.dll` with no codec wrapper compiled in. At runtime, `avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE)` returns NULL, and kimg_avif silently filters `avif`/`avifs` out of `QImageReader::supportedImageFormats()`. The plugin DLL still loads successfully — only the format filter is empty — so the bug looks like everything's fine. If you change libavif features after building kimageformats, **rebuild kimageformats** (`cmake --build build --config Release --target install --clean-first`) — codec availability is partly baked into kimg_avif via a `static const bool` cache that doesn't refresh.

7. **Release plugins can't be loaded by a debug Pixee build (or vice versa).** Qt's Release plugins import `Qt6Core.dll`; debug builds import `Qt6Cored.dll`. Cross-loading produces a clear error in the `QT_DEBUG_PLUGINS=1` log: "*incompatible Qt library. (Cannot mix debug and release libraries.)*" Match Pixee's build configuration to the plugin build configuration.

8. **PATH pollution from other apps' Qt installs can break plugin loads.** A separate app whose installer adds its own directory to `PATH` (with its own `Qt6Core.dll` / `Qt6Gui.dll`) wins the DLL lookup for plugin loads, producing "*cannot load: The specified module could not be found*" on every kimg_*.dll. The symptom looks like a missing dependency; the actual problem is a wrong-architecture or wrong-version Qt DLL found via PATH. Running `windeployqt` (which copies the right Qt DLLs next to `Pixee.exe`) takes the search path out of the equation. To audit, `where Qt6Gui.dll` from a fresh cmd shows what wins. Static analysis with [Dependencies](https://github.com/lucasg/Dependencies) (`Dependencies.exe -modules <plugin.dll>`) reveals the same thing under `[Environment]` entries.

9. **MSVC requires `[[maybe_unused]]`, not `__attribute__((unused))`.** The MinGW build accepts both; MSVC accepts only the standard form. Pixee's code stays portable to both compilers — don't reintroduce GCC-only attributes.

## Debugging plugin load failures

Set `QT_DEBUG_PLUGINS=1` either in Qt Creator's *Projects → Run → Run Environment* or in cmd before launching. The plugin loader will dump every discovery attempt:

- `loaded library` — success; format will appear in `supportedImageFormats()` provided the plugin's runtime capability check passes.
- `cannot load: The specified module could not be found` — a transitive DLL dependency isn't on the search path. Use `dumpbin /dependents <plugin.dll>` from a VS Developer Command Prompt for direct deps, or [Dependencies](https://github.com/lucasg/Dependencies) (`Dependencies.exe -modules`) for the full transitive graph including `[NOT_FOUND]` and `[Environment]` resolutions.
- `uses incompatible Qt library. (Cannot mix debug and release libraries.)` — see gotcha 7.

For the trickier case where the plugin loads but the format still doesn't appear (we hit this with AVIF), the plugin's `capabilities()` is filtering at runtime. Check the plugin's source for the gating condition — kimg_avif gates on `avifCodecName()` returning non-null, kimg_heif gates on libheif version, etc. To test the underlying library directly from PowerShell:

```powershell
Set-Location 'C:\Pixee\build\<kit>\release'
$env:PATH = "$PWD;$env:PATH"

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AvifCheck {
    [DllImport("avif.dll")]
    public static extern IntPtr avifCodecName(int choice, int flags);
    public static string Test(int flag) {
        var p = avifCodecName(0, flag);
        return p == IntPtr.Zero ? "NULL" : Marshal.PtrToStringAnsi(p);
    }
}
'@

"Decoder: $([AvifCheck]::Test(1))"   # Expect 'dav1d'
"Encoder: $([AvifCheck]::Test(2))"   # Expect 'aom'
```

If this returns `NULL`, the codec library is broken and the plugin can't possibly work; fix that first. If it returns a codec name, the plugin's wrapper is at fault — typically needs a rebuild against the updated codec library.
