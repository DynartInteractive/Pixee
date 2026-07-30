; Pixee installer  --  Inno Setup 6 script.
;
; Build:   scripts\build-installer.bat        (builds the portable, then compiles this)
;   or:    iscc installer\Pixee.iss           (if Pixee-portable\ already exists)
;
; Produces dist\Pixee-<ver>-setup.exe. UNSIGNED for now, so Windows SmartScreen
; will warn ("Windows protected your PC" -> More info -> Run anyway). To sign,
; see docs\installer.md Part 3 (Azure Trusted Signing / Key Vault) and set the
; SignTool= line noted below.

#define AppName      "Pixee"
; Version: single source is the repo-root VERSION.txt file. build-installer.bat
; passes it via /DAppVersion=...; a direct `iscc` run reads it here instead
; (SourcePath is this .iss's folder, so "..\VERSION.txt" is the repo root).
#ifndef AppVersion
  #define AppVersion Trim(FileRead(FileOpen(SourcePath + "..\VERSION.txt")))
#endif
#define AppPublisher "DynartInteractive"
#define AppExe       "Pixee.exe"
; Folder produced by scripts\make-portable.bat (build it with the MSVC kit for
; full HEIC/AVIF/PSD/XCF support).
#define SrcDir       "..\Pixee-portable"

[Setup]
; Keep this GUID STABLE across versions so upgrades replace rather than stack.
AppId={{AA5DBE77-8F94-486D-B014-67F1C99E6C6B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/DynartInteractive/Pixee
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=Pixee-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
; Per-machine install (Program Files + HKLM associations) needs elevation.
PrivilegesRequired=admin
; --- To code-sign, install a signer named "azuresign" (Tools > Configure Sign
; --- Tools) per docs\installer.md Part 3 and uncomment the next two lines:
; SignTool=azuresign
; SignedUninstaller=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Registry]
; --- ProgID: "a Pixee image" (icon + open verb) ---
Root: HKLM; Subkey: "Software\Classes\Pixee.Image"; ValueType: string; ValueData: "Image file"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Pixee.Image\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKLM; Subkey: "Software\Classes\Pixee.Image\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

; --- Application registration. SupportedTypes (below) is what puts Pixee in
; --- Explorer's "Open with" list; the base keys are declared here once. ---
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

; --- Capabilities: makes Pixee selectable in Settings > Default apps. Windows
; --- 10/11 blocks silently becoming the default; the user confirms there. ---
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Fast, minimalist image browser"
Root: HKLM; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "Software\{#AppName}\Capabilities"; Flags: uninsdeletevalue

; --- Per-extension associations (generated; keep in sync with the codecs the
; --- portable actually bundles). Three keys per type: add Pixee to the type's
; --- Open-With list, list the type under the app, and map it in Capabilities.
Root: HKLM; Subkey: "Software\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpg"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.jpeg\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpeg"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.jpe\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jpe"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jpe"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.jfif\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jfif"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".jfif"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".png"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.gif\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".gif"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".gif"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.bmp\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".bmp"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.dib\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".dib"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".dib"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.tiff\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tiff"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tiff"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.tif\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tif"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.webp\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webp"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.heic\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".heic"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heic"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.heif\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".heif"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".heif"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.avif\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".avif"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avif"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.psd\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".psd"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".psd"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.xcf\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".xcf"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xcf"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".svg"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svg"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.svgz\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".svgz"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".svgz"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.ico\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".ico"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ico"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.tga\OpenWithProgids"; ValueType: string; ValueName: "Pixee.Image"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tga"; ValueData: ""; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tga"; ValueData: "Pixee.Image"; Flags: uninsdeletevalue

[Code]
const
  SHCNE_ASSOCCHANGED = $08000000;
  SHCNF_IDLIST       = $0000;

procedure SHChangeNotify(wEventId: Integer; uFlags: Cardinal; dwItem1, dwItem2: Cardinal);
  external 'SHChangeNotify@shell32.dll stdcall';

procedure CurStepChanged(CurStep: TSetupStep);
begin
  // After the association keys are written, tell the shell so the new
  // Open-With entry appears without a logoff.
  if CurStep = ssPostInstall then
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
end;
