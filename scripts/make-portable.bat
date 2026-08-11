@echo off
setlocal enabledelayedexpansion

REM ===========================================================================
REM  make-portable.bat  --  Build a self-contained, portable Pixee for Windows.
REM
REM  Produces  <repo>\Pixee-portable\           (folder that runs with no Qt
REM                                               installed) and a matching
REM            <repo>\Pixee-<ver>-portable.zip   archive, where <ver> is read
REM                                               from the repo-root VERSION.txt.
REM
REM  Usage:
REM      scripts\make-portable.bat [QtKitDir]
REM
REM      QtKitDir   Optional. Path to the Qt kit to build with, e.g.
REM                 C:\Qt\6.11.1\mingw_64  or  C:\Qt\6.11.1\msvc2022_64
REM                 Defaults to the MinGW kit below. May also be set via the
REM                 PIXEE_QT_KIT environment variable.
REM
REM  MinGW kits build with mingw32-make and self-contain the compiler runtime.
REM  MSVC kits need nmake, so run this from an "x64 Native Tools Command Prompt
REM  for VS" (or after vcvars64.bat) so nmake and cl are on PATH.
REM
REM  HEIC / AVIF / PSD / XCF: bundled automatically ON MSVC BUILDS ONLY, from
REM  the prebuilt DLLs in thirdparty\imageformats\ (see the README there). The
REM  kimg_*.dll plugins link Qt6-MSVC and won't load into a MinGW build, so a
REM  MinGW portable is WebP-only. Build with the msvc2022_64 kit for HEIC/AVIF.
REM ===========================================================================

REM ---- Config (edit these defaults if your Qt lives elsewhere) --------------
set "QT_KIT=C:\Qt\6.11.1\mingw_64"
set "MINGW_TOOLS=C:\Qt\Tools\mingw1310_64\bin"
REM Prebuilt HEIC/AVIF/PSD/XCF plugins + codecs (codecs\ and plugins\ subdirs).
set "EXTRA_FORMATS_DIR=%~dp0..\thirdparty\imageformats"

REM Kit override: command-line arg wins, then PIXEE_QT_KIT env var.
if not "%~1"=="" set "QT_KIT=%~1"
if defined PIXEE_QT_KIT set "QT_KIT=%PIXEE_QT_KIT%"

REM ---- Resolve paths --------------------------------------------------------
pushd "%~dp0.."
set "REPO_ROOT=%CD%"
popd

REM Version: single source is the repo-root VERSION.txt, so the zip name matches
REM the app and the installer. Fall back to 0.0.0 if the file is somehow missing.
set "APPVER="
if exist "%REPO_ROOT%\VERSION.txt" set /p APPVER=<"%REPO_ROOT%\VERSION.txt"
if not defined APPVER set "APPVER=0.0.0"

set "WORK_DIR=%REPO_ROOT%\build-portable-work"
set "OUT_DIR=%REPO_ROOT%\Pixee-portable"
set "ZIP_FILE=%REPO_ROOT%\Pixee-%APPVER%-portable.zip"
REM qmake is happiest with forward slashes in DESTDIR.
set "OUT_DIR_FS=%OUT_DIR:\=/%"

REM ---- Pick the make tool based on the kit ----------------------------------
REM KIT_IS_MINGW gates both the make-tool choice and the (MSVC-only) extra
REM image-format bundling further down.
set "KIT_IS_MINGW="
echo %QT_KIT% | find /i "mingw" >nul
if not errorlevel 1 set "KIT_IS_MINGW=1"

if defined KIT_IS_MINGW (
    set "MAKE_TOOL=mingw32-make"
    set "PATH=%QT_KIT%\bin;%MINGW_TOOLS%;%PATH%"
) else (
    set "MAKE_TOOL=nmake"
    set "PATH=%QT_KIT%\bin;%PATH%"
)

echo(
echo === Pixee portable build ===
echo   Qt kit  : %QT_KIT%
echo   Make    : %MAKE_TOOL%
echo   Output  : %OUT_DIR%
echo(

REM ---- Sanity: qmake reachable? ---------------------------------------------
where qmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: qmake not found on PATH. Check the Qt kit path: %QT_KIT%
    goto :error
)

REM ---- Clean previous artifacts ---------------------------------------------
if exist "%WORK_DIR%" rmdir /s /q "%WORK_DIR%"
if exist "%OUT_DIR%"  rmdir /s /q "%OUT_DIR%"
if exist "%ZIP_FILE%" del /q "%ZIP_FILE%"
mkdir "%WORK_DIR%"

REM ---- Enable the Exiv2 metadata backend if its lib is present --------------
REM Auto-detect the drop-in (thirdparty\exiv2\, per docs\metadata.md). When
REM present we build with PIXEE_HAVE_EXIV2=1 so the portable ships full
REM EXIF/IPTC/XMP; otherwise the panel shows Qt-only basics. The matching DLL
REM copy + licence bundling happen after the build (see below).
set "EXIV2_QMAKE="
if exist "%REPO_ROOT%\thirdparty\exiv2\lib\exiv2.lib" (
    set "EXIV2_QMAKE=PIXEE_HAVE_EXIV2=1"
    echo   Exiv2 metadata backend: ENABLED
) else (
    echo   Exiv2 metadata backend: not present - metadata panel shows basics only.
)

REM ---- Configure + build (release only, straight into OUT_DIR) ---------------
pushd "%WORK_DIR%"
echo [1/4] qmake...
qmake "%REPO_ROOT%\Pixee.pro" "CONFIG+=release" "CONFIG-=debug_and_release" "DESTDIR=%OUT_DIR_FS%" %EXIV2_QMAKE%
if errorlevel 1 ( popd & goto :error )

echo [2/4] %MAKE_TOOL%...
%MAKE_TOOL%
if errorlevel 1 ( popd & goto :error )
popd

if not exist "%OUT_DIR%\Pixee.exe" (
    echo ERROR: build finished but %OUT_DIR%\Pixee.exe is missing.
    goto :error
)

REM ---- Deploy Qt DLLs, plugins, and the compiler runtime --------------------
echo [3/4] windeployqt...
"%QT_KIT%\bin\windeployqt.exe" --release --compiler-runtime "%OUT_DIR%\Pixee.exe"
if errorlevel 1 goto :error

REM MSVC: windeployqt --compiler-runtime does NOT reliably drop the VC++ CRT
REM DLLs (observed empty on Qt 6.11), so the portable would fault on any clean
REM machine without the VC++ redistributable. Copy them straight from the
REM toolset's redist dir (VCToolsRedistDir is set by vcvars). MinGW self-
REM contains its runtime via windeployqt, so this is MSVC-only.
REM Flat goto-guards on purpose: nested if() blocks trip the batch parser.
if defined KIT_IS_MINGW goto :after_crt
if not defined VCToolsRedistDir (
    echo   WARNING: VCToolsRedistDir unset - VC++ runtime NOT bundled.
    echo            Run from an x64 Native Tools Command Prompt to include it.
    goto :after_crt
)
set "CRT_DIR=%VCToolsRedistDir%\x64\Microsoft.VC143.CRT"
if not exist "%CRT_DIR%\vcruntime140.dll" (
    echo   WARNING: CRT not found under %CRT_DIR% - VC++ runtime NOT bundled.
    goto :after_crt
)
copy /Y "%CRT_DIR%\vcruntime140.dll"   "%OUT_DIR%\" >nul
copy /Y "%CRT_DIR%\vcruntime140_1.dll" "%OUT_DIR%\" >nul
copy /Y "%CRT_DIR%\msvcp140.dll"       "%OUT_DIR%\" >nul
echo   Bundled MSVC runtime (VC143 CRT).
:after_crt

REM ---- Extra image formats: HEIC / AVIF / PSD / XCF (MSVC builds only) -------
REM The kimg_*.dll plugins link Qt6-MSVC, so they only load into an MSVC build.
REM Plugins go into imageformats\ next to Qt's own; their codec DLLs go next to
REM Pixee.exe (the app dir is on the plugin dependency search path).
echo [4/4] Extra image formats (HEIC/AVIF/PSD/XCF)...
if defined KIT_IS_MINGW (
    echo   Skipped: MinGW build - kimg plugins are MSVC-only. WebP-only portable.
    goto :after_extra
)
if not exist "%EXTRA_FORMATS_DIR%\plugins" (
    echo   Skipped: %EXTRA_FORMATS_DIR% not found. WebP-only portable.
    goto :after_extra
)
copy /Y "%EXTRA_FORMATS_DIR%\plugins\kimg_*.dll" "%OUT_DIR%\imageformats\" >nul
if errorlevel 1 goto :error
copy /Y "%EXTRA_FORMATS_DIR%\codecs\*.dll" "%OUT_DIR%\" >nul
if errorlevel 1 goto :error
echo   Bundled kimg plugins + codec DLLs.
:after_extra

REM ---- Exiv2 metadata backend (only if built with PIXEE_HAVE_EXIV2) ---------
REM exiv2.dll (+ any dependency DLLs) must sit next to Pixee.exe for the
REM metadata panel's rich EXIF/IPTC/XMP. The binaries are a local drop-in
REM (thirdparty\exiv2\, git-ignored) — see docs\metadata.md. No-op when
REM absent, so a plain build without Exiv2 just skips this.
set "EXIV2_BIN=%REPO_ROOT%\thirdparty\exiv2\bin"
if exist "%EXIV2_BIN%\exiv2.dll" (
    copy /Y "%EXIV2_BIN%\*.dll" "%OUT_DIR%\" >nul
    if errorlevel 1 goto :error
    REM Exiv2 is GPLv2+; ship its licence next to the exe (see docs\metadata.md).
    if exist "%REPO_ROOT%\thirdparty\exiv2\COPYING" (
        copy /Y "%REPO_ROOT%\thirdparty\exiv2\COPYING" "%OUT_DIR%\Exiv2-COPYING.txt" >nul
    )
    echo   Bundled Exiv2 runtime DLLs + licence.
) else (
    echo   Skipped: no thirdparty\exiv2\bin\exiv2.dll - metadata panel shows basics only.
)

REM ---- Zip it up (tar ships with Windows 10 1803+) --------------------------
where tar >nul 2>&1
if not errorlevel 1 (
    echo Creating %ZIP_FILE% ...
    tar -a -c -f "%ZIP_FILE%" -C "%REPO_ROOT%" Pixee-portable
) else (
    echo NOTE: tar not found, skipping zip. The folder %OUT_DIR% is ready.
)

echo(
echo === Done ===
echo   Portable folder: %OUT_DIR%
if exist "%ZIP_FILE%" echo   Zip archive    : %ZIP_FILE%
echo(
echo Tip: test it where Qt is NOT on PATH to confirm it is self-contained.
endlocal
exit /b 0

:error
echo(
echo *** Build FAILED (see messages above). ***
endlocal
exit /b 1
