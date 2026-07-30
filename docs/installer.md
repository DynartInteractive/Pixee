# Building a signed installer that registers Pixee as an image manager

This covers turning a portable Pixee build into a proper Windows **installer**
that (a) installs into `Program Files`, (b) registers Pixee so it shows up in
Explorer's **Open with…** menu for image files, and (c) is **code-signed** with
the company's Azure signing keys.

It builds on `scripts\make-portable.bat` (see the top of `README.md` /
`thirdparty\imageformats\README.md`), which already produces a self-contained
`Pixee-portable\` folder — Qt DLLs, plugins, codec DLLs, VC++ runtime, and the
`themes\` tree. The installer just wraps that folder.

Pipeline overview:

```
make-portable.bat  ->  Pixee-portable\  ->  sign Pixee.exe  ->  Inno Setup  ->  sign setup.exe
```

## Status — the installer is implemented

The skeleton below is now a real, working project:

- **`installer\Pixee.iss`** — the full Inno Setup script (stable `AppId` GUID,
  the Open-With associations for all bundled formats, and the `SHChangeNotify`
  post-install/uninstall hook).
- **`scripts\build-installer.bat`** — builds the portable then compiles the
  installer in one step.

Build it (from an *x64 Native Tools Command Prompt for VS*, so the MSVC runtime
gets bundled — same requirement as `make-portable.bat`):

```cmd
scripts\build-installer.bat            :: defaults to the C:\Qt\6.11.1\msvc2022_64 kit
```

Output: `dist\Pixee-<ver>-setup.exe`. Needs **Inno Setup 6** (`winget install
JRSoftware.InnoSetup`). The build is currently **unsigned**, so SmartScreen
shows *"Windows protected your PC" → More info → Run anyway* on first run —
expected until the Part 3 signing below is wired in. The rest of this document
is the reference behind those two files; read it when changing the association
list or turning on signing.

## Why "Open with…" works at all

`Pixee.exe` accepts an image path as its first argument. `MainWindow.cpp`
(around line 454) walks `qApp->arguments().mid(1)`, and if an argument is an
existing file it sets `_startupImagePath`, navigates to that file's folder, and
opens the image in the viewer once the folder finishes loading (important on SMB
shares — the load is async). So the shell "open" verb below —
`"…\Pixee.exe" "%1"` — opens the clicked image directly, which is exactly the
behaviour a file association needs. No code change required.

## Part 1 — the installer (Inno Setup)

Recommended tool: **[Inno Setup](https://jrsoftware.org/isinfo.php)** — free,
one `.iss` script produces a single `setup.exe`, and it writes the association
registry keys natively. (WiX/MSI is the alternative only if the company needs an
**MSI** for Intune/enterprise deployment — heavier, skip it otherwise.)

Skeleton `installer\Pixee.iss` (fill in real values / GUID):

```ini
#define AppName    "Pixee"
#define AppVersion "1.0.0"
#define AppExe     "Pixee.exe"
; Folder produced by scripts\make-portable.bat (build it first, MSVC kit for
; full HEIC/AVIF support):
#define SrcDir     "..\Pixee-portable"

[Setup]
AppId={{PUT-A-FRESH-GUID-HERE}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename=Pixee-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
; 64-bit app:
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
; Signs both Pixee.exe and setup.exe via the "signtool" defined below (Part 3):
SignTool=azuresign
SignedUninstaller=yes

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Registry]
; (see Part 2 — the association keys go here)

[Code]
// Tell Explorer associations changed, so Open-With refreshes without a logoff.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    // SHCNE_ASSOCCHANGED = $08000000, SHCNF_IDLIST = 0
    // (call via a tiny helper or the built-in after registry writes)
end;
```

Build it: install Inno Setup, then `iscc installer\Pixee.iss` (or the IDE).
Note the placeholder `AppId` GUID — generate a real one and keep it stable
across versions so upgrades replace rather than stack.

## Part 2 — registering the Open-With association

These go in the `[Registry]` section. `Root: HKA` lets Inno pick HKLM for an
admin install and HKCU otherwise. Repeat `SupportedTypes` and `OpenWithProgids`
for every extension you want — ideally the set Pixee actually decodes
(`QImageReader::supportedImageFormats()`), e.g. jpg, jpeg, png, gif, bmp, tif,
tiff, webp, heic, heif, avif, psd, xcf, svg, ico, tga.

```ini
[Registry]
; --- A ProgID describing "a Pixee image" ---
Root: HKA; Subkey: "Software\Classes\Pixee.Image"; ValueType: string; ValueData: "Image file"
Root: HKA; Subkey: "Software\Classes\Pixee.Image\DefaultIcon"; ValueType: string; ValueData: "{app}\Pixee.exe,0"
Root: HKA; Subkey: "Software\Classes\Pixee.Image\shell\open\command"; ValueType: string; ValueData: """{app}\Pixee.exe"" ""%1"""

; --- Register the app so it appears in "Open with" (SupportedTypes is the key part) ---
Root: HKA; Subkey: "Software\Classes\Applications\Pixee.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Pixee"
Root: HKA; Subkey: "Software\Classes\Applications\Pixee.exe\shell\open\command"; ValueType: string; ValueData: """{app}\Pixee.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\Pixee.exe\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\Pixee.exe\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""
; ...one SupportedTypes line per extension...

; --- Add Pixee to each extension's Open-With list ---
Root: HKA; Subkey: "Software\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""
; ...one per extension...

; --- (Optional) Capabilities so Pixee shows in Settings > Default apps ---
Root: HKLM; Subkey: "Software\Pixee\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Pixee"
Root: HKLM; Subkey: "Software\Pixee\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Fast image browser"
Root: HKLM; Subkey: "Software\Pixee\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpg"; ValueData: "Pixee.Image"
Root: HKLM; Subkey: "Software\Pixee\Capabilities\FileAssociations"; ValueType: string; ValueName: ".png"; ValueData: "Pixee.Image"
Root: HKLM; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Pixee"; ValueData: "Software\Pixee\Capabilities"
```

Key facts:

- **`SupportedTypes`** under `Applications\Pixee.exe` is what makes Pixee appear
  in *Open with → Choose another app* for those types. `OpenWithProgids` per
  extension reinforces it.
- **You cannot silently become the default handler** on Windows 10/11 —
  Microsoft blocks programmatic default-setting; the user must confirm in
  *Settings → Default apps*. The `Capabilities` + `RegisteredApplications` keys
  are what make Pixee show up there as a choice.
- After writing keys, broadcast `SHChangeNotify(SHCNE_ASSOCCHANGED, …)` so
  Explorer refreshes immediately (the `[Code]` hook above), otherwise it updates
  on next logon.
- **Per-user vs per-machine:** an elevated installer writing `HKLM` (via `HKA`)
  associates for all users; a non-admin install falls back to `HKCU`. Both work.

## Part 3 — signing with the Azure keys

First determine **which** Azure signing product the company has — the command
differs:

| What you have | How to tell (Azure portal) | Tool |
|---|---|---|
| **Azure Trusted Signing** (formerly Azure Code Signing) | a *Trusted Signing account* + a *Certificate profile* resource | Microsoft `sign` CLI (`dotnet tool install --global sign`) |
| **Code-signing cert in Azure Key Vault** (OV/EV cert stored in a vault) | a *Key Vault* holding a certificate | **AzureSignTool** (`dotnet tool install --global AzureSignTool`) |

Search the portal for a **"Trusted Signing account"** resource: if it exists →
Trusted Signing; if instead there's just a **Key Vault** with a cert → Key Vault
path.

### Trusted Signing

Needs a service principal (or interactive `az login`) with the **Trusted Signing
Certificate Profile Signer** role, and the org identity must be validated.

```cmd
sign code trusted-signing ^
  --trusted-signing-account   <account-name> ^
  --trusted-signing-certificate-profile <profile-name> ^
  --trusted-signing-endpoint  https://<region>.codesigning.azure.net ^
  --description "Pixee" ^
  --timestamp-url http://timestamp.acs.microsoft.com ^
  "Pixee-portable\Pixee.exe"
```

### Azure Key Vault + AzureSignTool

Needs an app registration with `get`/`sign` permission on the certificate.

```cmd
AzureSignTool sign ^
  -kvu https://<vault-name>.vault.azure.net ^
  -kvi <app-client-id> -kvs <client-secret> ^
  -kvc <certificate-name> ^
  -tr http://timestamp.digicert.com -td sha256 -fd sha256 ^
  -v "Pixee-portable\Pixee.exe"
```

### Wiring it into Inno Setup

Define the signer once (Tools → Configure Sign Tools, or `iscc` `/S`), naming it
`azuresign` to match `SignTool=azuresign` in `[Setup]`:

```
azuresign=$q<path-to-signer-cmd>$q sign ... $f
```

Inno substitutes `$f` with each file to sign, so `SignTool=azuresign` signs the
uninstaller/installer automatically. Sign `Pixee.exe` **before** compiling the
installer (so the installed exe is signed) and let Inno sign the produced
`setup.exe`.

### Signing rules of thumb

- Sign **`Pixee.exe`** *and* the **installer `setup.exe`** — both, always. The
  bundled third-party codec DLLs (`heif.dll`, `avif.dll`, …) don't need the
  company signature.
- Always **timestamp** (`-tr` / `--timestamp-url`) so signatures survive cert
  expiry.
- SmartScreen reputation builds over downloads; Trusted Signing chains to a
  Microsoft-trusted root and warms up faster than a fresh OV cert.

## Suggested repo layout

```
installer\
  Pixee.iss            # Inno Setup script (Parts 1 + 2)
  sign.cmd             # wraps the Part 3 signer; reads creds from env, not committed
scripts\
  make-portable.bat    # existing: builds Pixee-portable\
```

End-to-end release:

```cmd
:: 1. build the portable folder (MSVC kit for HEIC/AVIF)
scripts\make-portable.bat C:\Qt\6.11.1\msvc2022_64
:: 2. sign the app exe (Part 3)
installer\sign.cmd Pixee-portable\Pixee.exe
:: 3. compile + sign the installer (SignTool=azuresign handles setup.exe)
iscc installer\Pixee.iss
```

## Open items / TODO

- ~~Generate a stable `AppId` GUID for `Pixee.iss`.~~ Done —
  `{AA5DBE77-8F94-486D-B014-67F1C99E6C6B}`. Keep it stable across versions.
- ~~Decide the exact extension list to register.~~ Done — the `[Registry]`
  block registers jpg/jpeg/jpe/jfif, png, gif, bmp/dib, tif/tiff, webp, heic,
  heif, avif, psd, xcf, svg/svgz, ico, tga. Keep this in sync with the codecs
  the portable actually bundles (`thirdparty\imageformats\`); it doesn't have
  to mirror every `supportedImageFormats()` entry.
- ~~Implement the `SHChangeNotify` call in the `[Code]` post-install step.~~
  Done — fired on both install and uninstall.
- Bump `#define AppVersion` in `Pixee.iss` per release (there's no version
  constant in the code yet to drive it from).
- **Signing (not yet wired):** confirm Trusted Signing vs Key Vault, add a
  signer named `azuresign` (Tools → Configure Sign Tools), and uncomment the
  `SignTool=azuresign` / `SignedUninstaller=yes` lines in `Pixee.iss`.
```
