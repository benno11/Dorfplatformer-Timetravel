#define MyAppName "Dorfplatformer Timetravel"
#ifndef MyAppVersion
  #define MyAppVersion "2.2.0 Beta 1"
#endif
#ifndef MyAppVersionId
  #define MyAppVersionId "20"
#endif
#define MyAppPublisher "Benno111"
#define MyAppExeName "df-launcher.exe"
#ifndef MyVcRedistFile
  #define MyVcRedistFile "vc_redist.x64.exe"
#endif

#ifdef MyInstallerIs64Bit
  #define MyInstallIn64BitMode "x64"
#endif

[Setup]
AppId={{D1BE0AE1-0D76-4A53-B02D-1CC6C36AB3AB}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
WizardStyle=classic
#ifdef MyInstallIn64BitMode
ArchitecturesInstallIn64BitMode={#MyInstallIn64BitMode}
#endif
PrivilegesRequired=lowest
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
Source: "..\..\dist\windows-installer\{#MyVcRedistFile}"; DestDir: "{tmp}"; Flags: deleteafterinstall

[InstallDelete]
; Clean up files left by the legacy single-folder install layout.
; The side-by-side versioned payload now lives under {app}\versions\<id>,
; so these root-level files are unused after upgrading.
Type: filesandordirs; Name: "{app}\assets"
Type: files; Name: "{app}\platformer.exe"
Type: files; Name: "{app}\sheet_config.exe"
Type: files; Name: "{app}\df-updater.exe"
Type: files; Name: "{app}\df-tray.exe"
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
Name: "{group}\Dorfplatformer Tray"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--tray"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\Dorfplatformer Tray"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--tray"; WorkingDir: "{app}"; Tasks: startuptray

[Run]
Filename: "{tmp}\{#MyVcRedistFile}"; Parameters: "/install /quiet /norestart"; Flags: waituntilterminated
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  ExistingSystemInstallHandled: Boolean;

function GetExistingSystemUninstallKey(): string;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{D1BE0AE1-0D76-4A53-B02D-1CC6C36AB3AB}_is1';
end;

function ExtractCommandExe(const CommandLine: string): string;
var
  S: string;
  I: Integer;
begin
  S := Trim(CommandLine);
  if S = '' then begin
    Result := '';
    exit;
  end;

  if S[1] = '"' then begin
    Delete(S, 1, 1);
    I := Pos('"', S);
    if I > 0 then
      Result := Copy(S, 1, I - 1)
    else
      Result := S;
  end else begin
    I := Pos(' ', S);
    if I > 0 then
      Result := Copy(S, 1, I - 1)
    else
      Result := S;
  end;
end;

function ExtractCommandParams(const CommandLine: string): string;
var
  S: string;
  I: Integer;
begin
  S := Trim(CommandLine);
  if S = '' then begin
    Result := '';
    exit;
  end;

  if S[1] = '"' then begin
    Delete(S, 1, 1);
    I := Pos('"', S);
    if I > 0 then
      Result := Trim(Copy(S, I + 1, MaxInt))
    else
      Result := '';
  end else begin
    I := Pos(' ', S);
    if I > 0 then
      Result := Trim(Copy(S, I + 1, MaxInt))
    else
      Result := '';
  end;
end;

function GetExistingSystemUninstallCommand(var CommandLine: string): Boolean;
var
  KeyName: string;
begin
  KeyName := GetExistingSystemUninstallKey();
  CommandLine := '';
  Result :=
    RegQueryStringValue(HKLM64, KeyName, 'QuietUninstallString', CommandLine) or
    RegQueryStringValue(HKLM64, KeyName, 'UninstallString', CommandLine) or
    RegQueryStringValue(HKLM32, KeyName, 'QuietUninstallString', CommandLine) or
    RegQueryStringValue(HKLM32, KeyName, 'UninstallString', CommandLine);
  Result := Result and (CommandLine <> '');
end;

function RemoveExistingSystemInstall(): Boolean;
var
  CommandLine: string;
  ExePath: string;
  Params: string;
  ResultCode: Integer;
begin
  Result := True;
  if ExistingSystemInstallHandled then
    exit;

  ExistingSystemInstallHandled := True;
  if not GetExistingSystemUninstallCommand(CommandLine) then
    exit;

  ExePath := ExtractCommandExe(CommandLine);
  Params := ExtractCommandParams(CommandLine);
  if ExePath = '' then begin
    Result := False;
    exit;
  end;

  if Pos('/VERYSILENT', Uppercase(Params)) = 0 then
    Params := Params + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART';

  Log('Removing existing system-wide install: ' + CommandLine);
  if not ShellExec('runas', ExePath, Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then begin
    Log('Failed to launch existing system uninstaller.');
    Result := False;
    exit;
  end;

  if ResultCode <> 0 then begin
    Log(Format('Existing system uninstaller returned exit code %d.', [ResultCode]));
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not RemoveExistingSystemInstall() then
    Result := 'The existing system-wide installation could not be removed. Please uninstall it first, then run the installer again.';
end;
