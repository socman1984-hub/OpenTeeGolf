; The OpenTeeGolf Unreal game's installer (Inno Setup 6): OpenTeeGolf-Setup-<version>.exe.
;
; It holds the game without its tree scenery (pakchunk1-3, about 3 GB: too big to ship in one file
; on GitHub) and installs it for the current user, no admin needed - in
; %LOCALAPPDATA%\Programs\OpenTeeGolf, where the game can update its own files. On its first start
; the game's updater finds the scenery listed in manifest.json and missing, downloads it and
; restarts; after that it fetches only what each new release changes.
;
; Built by unreal/publish_release.py (with the published manifest.json, which says where each file
; downloads from) and by make_release.ps1 (a local one, for testing):
;   ISCC.exe /DVersion=0.16.0 /DSource=<release folder> /DOutDir=<folder> OpenTeeGolf.iss

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #error Pass /DSource=<the release folder>
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{6D2E8B1A-4C7F-4E39-9B52-0F3A7C1D8E64}
AppName=OpenTeeGolf
AppVersion={#Version}
AppVerName=OpenTeeGolf {#Version}
AppPublisher=OpenTeeGolf
AppPublisherURL=https://github.com/socman1984-hub/OpenTeeGolf
DefaultDirName={localappdata}\Programs\OpenTeeGolf
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=OpenTeeGolf-Setup-{#Version}
SetupIconFile=..\OpenTeeGolf\Build\Windows\Application.ico
UninstallDisplayIcon={app}\OpenTeeGolf.exe
UninstallDisplayName=OpenTeeGolf
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; (the game updates itself: a newer installer just installs over it)
UsePreviousAppDir=yes
CloseApplications=yes

[Messages]
FinishedLabel=OpenTeeGolf is installed.%n%nOn its first start it downloads its scenery (about 3 GB) and restarts once. Your courses come from the web version's Export to Unreal.

[Tasks]
Name: "desktopicon"; Description: "Make a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Excludes: "pakchunk1*,pakchunk2*,pakchunk3*,\Update"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\OpenTeeGolf"; Filename: "{app}\OpenTeeGolf.exe"
Name: "{autodesktop}\OpenTeeGolf"; Filename: "{app}\OpenTeeGolf.exe"; Tasks: desktopicon

[Run]
; (Microsoft's Visual C++ runtime, if this PC hasn't got it: it asks for admin to install)
Filename: "{app}\Engine\Extras\Redist\en-us\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: shellexec waituntilterminated; Verb: runas; Check: NeedsVCRuntime
Filename: "{app}\OpenTeeGolf.exe"; Description: "Start OpenTeeGolf"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; (what the game downloaded or wrote itself, not installed by this: the scenery, updates in progress)
Type: filesandordirs; Name: "{app}\OpenTeeGolf"
Type: filesandordirs; Name: "{app}\Engine"
Type: filesandordirs; Name: "{app}\Update"
Type: filesandordirs; Name: "{app}\bridges"
Type: files; Name: "{app}\manifest.json"
Type: dirifempty; Name: "{app}"

[Code]
// The x64 Visual C++ 2015-2022 runtime installed (its own registry key)
function NeedsVCRuntime: Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1));
end;
