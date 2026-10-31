; PhaseForge - Windows installer (Inno Setup 6)
; Built by CI with:  ISCC.exe /DAppVersion=1.0.0 /DBuildDir=<...>\PhaseForge_artefacts\Release PhaseForge.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\PhaseForge_artefacts\Release"
#endif

[Setup]
; Never change this GUID after your first public release (Windows uses it to find upgrades/uninstall).
AppId={{9E34917C-C04F-4B66-8E75-5822EDFBE803}
AppName=PhaseForge
AppVersion={#AppVersion}
AppPublisher=PhaseForge
DefaultDirName={autopf}\PhaseForge
DefaultGroupName=PhaseForge
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=PhaseForge-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=PhaseForge
DisableDirPage=auto

[Types]
Name: "full";   Description: "Full installation (VST3 plugin + standalone app)"
Name: "plugin"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3";       Description: "PhaseForge VST3 plugin (for Ableton, FL Studio, Reaper, ...)"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "PhaseForge standalone app";                                     Types: full custom

[Files]
; VST3 is a folder bundle on Windows -> goes to the standard shared VST3 folder, where every DAW looks.
Source: "{#BuildDir}\VST3\PhaseForge.vst3\*"; DestDir: "{commoncf64}\VST3\PhaseForge.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\PhaseForge.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\PhaseForge (standalone)"; Filename: "{app}\PhaseForge.exe"; Components: standalone
Name: "{group}\Uninstall PhaseForge";    Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\PhaseForge.vst3"

[Run]
Filename: "{app}\PhaseForge.exe"; Description: "Launch PhaseForge standalone"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone
