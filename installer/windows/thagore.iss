; Thagore Windows UI Installer (Inno Setup)

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

#define MyAppName "Thagore Compiler"
#define MyAppPublisher "Thagore Foundation"
#define MyAppExeName "thagore.exe"

[Setup]
AppId={{40A6C9A9-6E1A-4E2F-9D89-4EC2EC8E67A0}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Thagore
DefaultGroupName=Thagore
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
OutputDir=.
OutputBaseFilename=thagore-windows-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible or arm64
ArchitecturesInstallIn64BitMode=x64compatible or arm64
PrivilegesRequired=admin
SetupLogging=yes

[Tasks]
Name: "addtopath"; Description: "Add Thagore to PATH"; Flags: checkedonce
Name: "installllvm"; Description: "Install LLVM 21.1.8 automatically (winget)"; Flags: unchecked

[InstallDelete]
Type: files; Name: "{app}\bin\thagore.exe"
Type: files; Name: "{app}\bin\thag.exe"
Type: files; Name: "{app}\bin\thagore.cmd"

[Files]
Source: "..\..\dist\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\dist\lib\*"; DestDir: "{app}\lib"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\dist\installer\*"; DestDir: "{app}\installer"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\windows.ps1"" -yes"; \
  StatusMsg: "Installing LLVM 21.1.8 via winget..."; \
  Flags: runhidden waituntilterminated; \
  Tasks: installllvm

[Code]
const
  EnvKeyMachine = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  EnvKeyUser = 'Environment';
  EnvValueName = 'Path';
  WM_SETTINGCHANGE_MSG = $001A;
  SMTO_ABORTIFHUNG = $0002;

function SendMessageTimeout(hWnd: Integer; Msg: Integer; wParam: Integer; lParam: string; fuFlags: Integer; uTimeout: Integer; var lpdwResult: Integer): Integer;
  external 'SendMessageTimeoutW@user32.dll stdcall';

function NormalizePath(const Value: string): string;
begin
  Result := UpperCase(Trim(Value));
  while (Length(Result) > 0) and (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function SplitSemicolon(const Value: string): TArrayOfString;
var
  Work: string;
  P: Integer;
  Item: string;
begin
  SetArrayLength(Result, 0);
  Work := Value;
  while True do
  begin
    P := Pos(';', Work);
    if P = 0 then
    begin
      SetArrayLength(Result, GetArrayLength(Result) + 1);
      Result[GetArrayLength(Result) - 1] := Work;
      Break;
    end;

    Item := Copy(Work, 1, P - 1);
    SetArrayLength(Result, GetArrayLength(Result) + 1);
    Result[GetArrayLength(Result) - 1] := Item;
    Delete(Work, 1, P);
  end;
end;

function PathContainsEntry(const PathValue, Entry: string): Boolean;
var
  CurrentParts: TArrayOfString;
  I: Integer;
  Candidate: string;
begin
  Result := False;
  CurrentParts := SplitSemicolon(PathValue);
  for I := 0 to GetArrayLength(CurrentParts) - 1 do
  begin
    Candidate := NormalizePath(CurrentParts[I]);
    if (Candidate <> '') and (Candidate = NormalizePath(Entry)) then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function JoinNonEmpty(const Parts: TArrayOfString): string;
var
  I: Integer;
  OutValue: string;
begin
  OutValue := '';
  for I := 0 to GetArrayLength(Parts) - 1 do
  begin
    if Trim(Parts[I]) = '' then
      Continue;
    if OutValue = '' then
      OutValue := Trim(Parts[I])
    else
      OutValue := OutValue + ';' + Trim(Parts[I]);
  end;
  Result := OutValue;
end;

function WritePath(AllUsers: Boolean; const NewPath: string): Boolean;
begin
  if AllUsers then
    Result := RegWriteStringValue(HKLM, EnvKeyMachine, EnvValueName, NewPath)
  else
    Result := RegWriteStringValue(HKCU, EnvKeyUser, EnvValueName, NewPath);
end;

function ReadPath(AllUsers: Boolean; var CurrentPath: string): Boolean;
begin
  if AllUsers then
    Result := RegQueryStringValue(HKLM, EnvKeyMachine, EnvValueName, CurrentPath)
  else
    Result := RegQueryStringValue(HKCU, EnvKeyUser, EnvValueName, CurrentPath);
end;

function AddPathEntry(AllUsers: Boolean; const Entry: string): Boolean;
var
  CurrentPath: string;
begin
  if not ReadPath(AllUsers, CurrentPath) then
    CurrentPath := '';

  if PathContainsEntry(CurrentPath, Entry) then
  begin
    Result := True;
    Exit;
  end;

  if Trim(CurrentPath) = '' then
    Result := WritePath(AllUsers, Entry)
  else
    Result := WritePath(AllUsers, CurrentPath + ';' + Entry);
end;

function RemovePathEntry(AllUsers: Boolean; const Entry: string): Boolean;
var
  CurrentPath: string;
  CurrentParts: TArrayOfString;
  Filtered: TArrayOfString;
  I: Integer;
  Candidate: string;
begin
  Result := True;
  if not ReadPath(AllUsers, CurrentPath) then
    Exit;

  CurrentParts := SplitSemicolon(CurrentPath);
  SetArrayLength(Filtered, 0);
  for I := 0 to GetArrayLength(CurrentParts) - 1 do
  begin
    Candidate := Trim(CurrentParts[I]);
    if Candidate = '' then
      Continue;
    if NormalizePath(Candidate) = NormalizePath(Entry) then
      Continue;
    SetArrayLength(Filtered, GetArrayLength(Filtered) + 1);
    Filtered[GetArrayLength(Filtered) - 1] := Candidate;
  end;

  Result := WritePath(AllUsers, JoinNonEmpty(Filtered));
end;

procedure BroadcastPathChange;
var
  SendResult: Integer;
begin
  SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE_MSG, 0, 'Environment', SMTO_ABORTIFHUNG, 5000, SendResult);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  BinPath: string;
  OkMachine: Boolean;
  OkUser: Boolean;
begin
  if CurStep <> ssPostInstall then
    Exit;

  if not WizardIsTaskSelected('addtopath') then
    Exit;

  BinPath := ExpandConstant('{app}\bin');
  OkMachine := AddPathEntry(True, BinPath);
  OkUser := AddPathEntry(False, BinPath);

  if not (OkMachine or OkUser) then
  begin
    MsgBox(
      'Could not update PATH automatically. Please add manually: ' + BinPath,
      mbError,
      MB_OK
    );
    Exit;
  end;

  BroadcastPathChange;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  BinPath: string;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  BinPath := ExpandConstant('{app}\bin');
  RemovePathEntry(True, BinPath);
  RemovePathEntry(False, BinPath);
  BroadcastPathChange;
end;
