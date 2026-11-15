; Setup for Headroom, built with Inno Setup (free, https://jrsoftware.org/isinfo.php).
;
;   iscc /DAppVersion=2.1.0-alpha1 installer\headroom.iss
;
; It installs for the current user without administrator rights (the wizard offers "for all users" as well),
; adds a Start menu entry, and can put a desktop icon and "Start with Windows" in place. Uninstalling removes the
; program and the autostart entry; the settings stay unless you choose to delete them.

#ifndef AppVersion
  #define AppVersion "2.1.0-alpha3"
#endif

[Setup]
AppId={{4A15DC9F-4C7C-4311-A9CD-2C609240C960}
AppName=Headroom
AppVersion={#AppVersion}
AppPublisher=Jakob Hüttner
AppPublisherURL=https://github.com/jakobtech/headroom
AppSupportURL=https://github.com/jakobtech/headroom/issues
AppUpdatesURL=https://github.com/jakobtech/headroom/releases
DefaultDirName={autopf}\Headroom
DefaultGroupName=Headroom
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
LicenseFile=..\LICENSE
SetupIconFile=..\src\headroom.ico
UninstallDisplayIcon={app}\Headroom.exe
UninstallDisplayName=Headroom
OutputDir=Output
OutputBaseFilename=Headroom-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start Headroom with Windows"; GroupDescription: "Options:"; Flags: unchecked

[Files]
Source: "..\bin\Headroom.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Headroom"; Filename: "{app}\Headroom.exe"
Name: "{autodesktop}\Headroom"; Filename: "{app}\Headroom.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Headroom"; ValueData: """{app}\Headroom.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\Headroom.exe"; Description: "{cm:LaunchProgram,Headroom}"; Flags: nowait postinstall skipifsilent

[Code]
// Asks a running Headroom to close the way its tray menu does, so that every app is routed back to its output.
procedure CloseRunningHeadroom;
var
  Window: HWND;
  ResultCode, Tries: Integer;
begin
  Window := FindWindowByClassName('HeadroomMixer');
  if Window <> 0 then
  begin
    PostMessage(Window, $0010, 0, 0);  // WM_CLOSE
    Tries := 0;
    while (FindWindowByClassName('HeadroomMixer') <> 0) and (Tries < 50) do
    begin
      Sleep(100);
      Tries := Tries + 1;
    end;
    if FindWindowByClassName('HeadroomMixer') <> 0 then
      Exec('taskkill.exe', '/IM Headroom.exe /F', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningHeadroom;
  Result := '';
end;

function InitializeUninstall: Boolean;
begin
  CloseRunningHeadroom;
  Result := True;
end;

procedure CurrentUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Settings, AutostartCommand: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // The program can switch "Start with Windows" on by itself: that entry goes too, but only if it
    // starts this installation (another copy of the program keeps its own).
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Headroom', AutostartCommand) then
      if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(AutostartCommand)) > 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Headroom');
    Settings := ExpandConstant('{userappdata}\Headroom');
    if DirExists(Settings) and (not UninstallSilent) then
    begin
      if MsgBox('Delete your Headroom settings too (equalizers, loudness settings, hidden lists)?' + #13#10 +
                'Choose No to keep them for a later installation.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(Settings, True, True, True);
        DelTree(ExpandConstant('{localappdata}\Headroom'), True, True, True);
      end;
    end;
  end;
end;
