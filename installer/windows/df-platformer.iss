#define MyAppName "Dorfplatformer Timetravel"
#ifndef MyAppVersion
  #define MyAppVersion "2.2.0"
#endif
#ifndef MyAppVersionId
  #define MyAppVersionId "22"
#endif
#define MyAppPublisher "Benno111"
#define MyAppExeName "df-launcher.exe"

[Setup]
AppId={{D1BE0AE1-0D76-4A53-B02D-1CC6C36AB3AB}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}  // Display a message box
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
WizardStyle=classic
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=..\..\dist\windows-installer\output
OutputBaseFilename=df-platformer-windows-installer

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "startuptray"; Description: "Start tray companion with Windows"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "..\..\dist\windows-installer\root\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\dist\windows-installer\app\*"; DestDir: "{app}\versions\{#MyAppVersionId}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\dist\windows-installer\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[InstallDelete]
; Clean up files left by the legacy single-folder install layout.
; The side-by-side versioned payload now lives under {app}\versions\<id>,
; so these root-level files are unused after upgrading.
Type: filesandordirs; Name: "{app}\assets"
Type: files; Name: "{app}\platformer.exe"
Type: files; Name: "{app}\sheet_config.exe"
Type: files; Name: "{app}\df-updater.exe"
Type: files; Name: "{app}\SDL3.dll"
Type: files; Name: "{app}\SDL3_image.dll"
Type: files; Name: "{app}\SDL3_mixer.dll"
Type: files; Name: "{app}\SDL3_ttf.dll"
Type: files; Name: "{app}\brotlicommon.dll"
Type: files; Name: "{app}\brotlidec.dll"
Type: files; Name: "{app}\brotlienc.dll"
Type: files; Name: "{app}\bz2.dll"
Type: files; Name: "{app}\freetype.dll"
Type: files; Name: "{app}\libcurl.dll"
Type: files; Name: "{app}\libgme.dll"
Type: files; Name: "{app}\libogg-0.dll"
Type: files; Name: "{app}\libopus-0.dll"
Type: files; Name: "{app}\libopusfile-0.dll"
Type: files; Name: "{app}\libpng16.dll"
Type: files; Name: "{app}\libwavpack-1.dll"
Type: files; Name: "{app}\libxmp.dll"
Type: files; Name: "{app}\zlib1.dll"
Type: files; Name: "{app}\object_type_map.json"
Type: files; Name: "{app}\README.md"
Type: files; Name: "{app}\LICENSE"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\Dorfplatformer Tray"; Filename: "{app}\df-tray.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\Dorfplatformer Tray"; Filename: "{app}\df-tray.exe"; WorkingDir: "{app}"; Tasks: startuptray

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; Flags: waituntilterminated
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
