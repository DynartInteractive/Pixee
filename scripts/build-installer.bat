@echo off
setlocal

REM ===========================================================================
REM  build-installer.bat  --  Build the portable, then compile the installer.
REM
REM  Produces  <repo>\dist\Pixee-<ver>-setup.exe   (unsigned).
REM
REM  Usage:
REM      scripts\build-installer.bat [QtKitDir]
REM
REM      QtKitDir   Optional. Qt kit to build with. Defaults to the MSVC kit
REM                 (needed for HEIC/AVIF/PSD/XCF). Run from an "x64 Native
REM                 Tools Command Prompt for VS" so nmake/cl are on PATH and
REM                 the VC++ runtime gets bundled.
REM
REM  Requires Inno Setup 6 (ISCC.exe). If missing:
REM      winget install JRSoftware.InnoSetup
REM  or download from https://jrsoftware.org/isdl.php
REM ===========================================================================

set "QTKIT=%~1"
if "%QTKIT%"=="" set "QTKIT=C:\Qt\6.11.1\msvc2022_64"

REM ---- 1. Build the self-contained portable folder --------------------------
call "%~dp0make-portable.bat" "%QTKIT%"
if errorlevel 1 (
    echo *** Portable build failed - installer not compiled. ***
    exit /b 1
)

REM ---- 2. Locate the Inno Setup compiler ------------------------------------
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
REM winget installs Inno Setup per-user by default:
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo(
    echo ERROR: Inno Setup 6 not found ^(ISCC.exe^).
    echo   Install it:  winget install JRSoftware.InnoSetup
    echo   or download: https://jrsoftware.org/isdl.php
    exit /b 1
)

REM ---- 3. Compile the installer ---------------------------------------------
REM Version: single source is the repo-root VERSION file. Read its first line
REM and hand it to ISCC so the setup filename/metadata match the app exactly.
set "APPVER="
set /p APPVER=<"%~dp0..\VERSION.txt"
echo(
echo === Compiling installer with Inno Setup (v%APPVER%) ===
"%ISCC%" /DAppVersion=%APPVER% "%~dp0..\installer\Pixee.iss"
if errorlevel 1 (
    echo *** Installer compile FAILED. ***
    exit /b 1
)

echo(
echo === Installer built ===
dir /b "%~dp0..\dist\Pixee-*-setup.exe"
echo   ^(in %~dp0..\dist^)
echo(
echo NOTE: unsigned - SmartScreen will warn on first run. See docs\installer.md
echo       Part 3 to add Azure signing.
endlocal
exit /b 0
