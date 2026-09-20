@echo off
REM Regenerates the two icons built from the source SVG:
REM   resources\icons\Pixee.ico   -- the Windows .exe / installer icon
REM   docs\pixee-icon-256.png     -- the README header image
REM
REM One icon, two consumers: Pixee.pro's RC_ICONS folds it into the .exe, and
REM installer\Pixee.iss uses it as SetupIconFile. Both read the checked-in .ico,
REM so run this after editing resources\icons\net.dynart.Pixee.svg and commit
REM the result -- a build must not need ImageMagick.
REM
REM Needs ImageMagick 7 with the librsvg delegate (`magick -list format` should
REM show SVG as RSVG, not MSVG -- MSVG renders the gradients and clip path wrong).
setlocal
cd /d "%~dp0.."

where magick >nul 2>&1
if errorlevel 1 (
    echo ERROR: ImageMagick 'magick' not found on PATH.
    exit /b 1
)

REM Render once at 1024 and let the ICO coder downsample, so every size is
REM filtered from the same high-res raster. 256 is stored PNG-compressed by
REM the coder; the rest are 32-bit BGRA bitmaps. 20 and 40 are the 125% and
REM 250% Explorer scalings -- without them Windows resamples 16/32 and smears.
magick -background none -density 1024 resources\icons\net.dynart.Pixee.svg ^
    -resize 1024x1024 ^
    -define icon:auto-resize=256,128,64,48,40,32,24,20,16 ^
    resources\icons\Pixee.ico
if errorlevel 1 exit /b 1

REM The README header image. Same source, so the docs icon can never drift
REM from the one the .exe ships. Transparent background: GitHub renders the
REM README on white in light mode and near-black in dark, and a baked-in
REM ground would show as a square in one of them.
magick -background none -density 1024 resources\icons\net.dynart.Pixee.svg ^
    -resize 256x256 ^
    docs\pixee-icon-256.png
if errorlevel 1 exit /b 1

echo Wrote resources\icons\Pixee.ico and docs\pixee-icon-256.png
