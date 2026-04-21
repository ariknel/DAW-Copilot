; AI MIDI Composer - Inno Setup 6 Script
; Pure ASCII only

#define AppName     "AI MIDI Composer"
#define AppVersion  "0.1.0"
#define AppPublisher "Arik"
#define VST3Name    "AI MIDI Composer.vst3"
#define InstDir     "{pf}\AIMidiComposer"
#define SidecarDir  "{pf}\AIMidiComposer\sidecar"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppId={{8a7f4b4c-3e5d-4e6a-9b12-21c9a7f58ab0}
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
DefaultDirName={pf}\AIMidiComposer
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
DisableWelcomePage=no
AllowNoIcons=yes
OutputDir=dist
OutputBaseFilename=AIMidiComposer-Installer

; venv.zip is already compressed - recompressing wastes time
; Store it uncompressed so Inno compiles fast and extraction is fast
Compression=none
SolidCompression=no

PrivilegesRequired=admin
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
Type: filesandordirs; Name: "{pf}\Common Files\VST3\{#VST3Name}"
Type: filesandordirs; Name: "{pf}\AIMidiComposer\sidecar"
Type: filesandordirs; Name: "{pf}\AIMidiComposer\venv"

[Files]
; VST3 plugin (small - compress this)
Source: "build\AIMidiComposerVST_artefacts\Release\VST3\{#VST3Name}\*"; \
    DestDir: "{pf}\Common Files\VST3\{#VST3Name}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Sidecar scripts (tiny)
Source: "sidecar\dist\sidecar\*"; \
    DestDir: "{#SidecarDir}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Python venv as pre-compressed zip (store as-is, no recompression)
Source: "sidecar\dist\venv.zip"; \
    DestDir: "{#InstDir}"; \
    Flags: ignoreversion nocompression

; Python extractor script (fast extraction vs PowerShell)
Source: "installer\extract_venv.py"; \
    DestDir: "{#InstDir}"; \
    Flags: ignoreversion

; README
Source: "docs\README.txt"; \
    DestDir: "{#InstDir}"; \
    Flags: ignoreversion

[Run]
; Use the system Python to extract venv (much faster than PowerShell Expand-Archive)
; Falls back to PowerShell if Python not on PATH
Filename: "cmd.exe"; \
    Parameters: "/c python ""{#InstDir}\extract_venv.py"" ""{#InstDir}\venv.zip"" ""{#InstDir}\venv"" || powershell -NoProfile -Command ""Expand-Archive '{#InstDir}\venv.zip' '{#InstDir}\venv' -Force; Remove-Item '{#InstDir}\venv.zip'"""; \
    StatusMsg: "Extracting Python environment..."; \
    Flags: runhidden waituntilterminated

; Firewall rule
Filename: "netsh"; \
    Parameters: "advfirewall firewall add rule name=""AI MIDI Composer Sidecar"" dir=in action=allow program=""{pf}\AIMidiComposer\venv\Scripts\python.exe"" enable=yes profile=any"; \
    Flags: runhidden

[UninstallRun]
Filename: "netsh"; \
    Parameters: "advfirewall firewall delete rule name=""AI MIDI Composer Sidecar"""; \
    Flags: runhidden

[UninstallDelete]
Type: filesandordirs; Name: "{pf}\Common Files\VST3\{#VST3Name}"
Type: filesandordirs; Name: "{pf}\AIMidiComposer"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var Msg: String;
begin
  if CurStep = ssDone then
  begin
    Msg := '{#AppName} installed.' + #13#10 + #13#10 +
           'VST3: C:\Program Files\Common Files\VST3\{#VST3Name}' + #13#10 +
           'Engine: C:\Program Files\AIMidiComposer\sidecar\' + #13#10 + #13#10 +
           'Next steps:' + #13#10 +
           '  1. Open DAW and rescan VST3 plugins' + #13#10 +
           '  2. Add "AI MIDI Composer" to a MIDI track' + #13#10 +
           '  3. First use downloads the AI model (~4 GB)';
    MsgBox(Msg, mbInformation, MB_OK);
  end;
end;
