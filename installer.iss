; AI MIDI Composer - Inno Setup 5 Script
; Pure ASCII only - no Unicode anywhere
;
; Layout after install:
;   VST3:    C:\Program Files\Common Files\VST3\AI MIDI Composer.vst3\
;   Sidecar: C:\Program Files\AIMidiComposer\sidecar\  (main.py + sidecar.cmd)
;   Venv:    C:\Program Files\AIMidiComposer\venv\     (extracted from venv.zip)

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
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
Type: filesandordirs; Name: "{pf}\Common Files\VST3\{#VST3Name}"
Type: filesandordirs; Name: "{pf}\AIMidiComposer\sidecar"
Type: filesandordirs; Name: "{pf}\AIMidiComposer\venv"

[Files]
; VST3 plugin
Source: "build\AIMidiComposerVST_artefacts\Release\VST3\{#VST3Name}\*"; \
    DestDir: "{pf}\Common Files\VST3\{#VST3Name}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Sidecar entrypoint (main.py + sidecar.cmd only - small, fast)
Source: "sidecar\dist\sidecar\*"; \
    DestDir: "{#SidecarDir}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Venv as a single zip (avoids Windows Defender scanning 50k individual files)
Source: "sidecar\dist\venv.zip"; \
    DestDir: "{#InstDir}"; \
    Flags: ignoreversion

; README
Source: "docs\README.txt"; \
    DestDir: "{#InstDir}"; \
    Flags: ignoreversion

[Run]
; Extract venv.zip using PowerShell (built into Windows 10+)
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -Command ""Expand-Archive -Path '{#InstDir}\venv.zip' -DestinationPath '{#InstDir}\venv' -Force"""; \
    StatusMsg: "Extracting Python environment (this may take a minute)..."; \
    Flags: runhidden waituntilterminated

; Delete the zip after extraction to save disk space
Filename: "cmd.exe"; \
    Parameters: "/c del /q ""{#InstDir}\venv.zip"""; \
    Flags: runhidden waituntilterminated

; Firewall rule for python.exe in venv
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
var
  Msg: String;
begin
  if CurStep = ssDone then
  begin
    Msg := '{#AppName} installed.' + #13#10 + #13#10 +
           'VST3: C:\Program Files\Common Files\VST3\{#VST3Name}' + #13#10 +
           'Engine: C:\Program Files\AIMidiComposer\sidecar\' + #13#10 + #13#10 +
           'Next steps:' + #13#10 +
           '  1. Open your DAW' + #13#10 +
           '  2. Rescan VST3 plugins' + #13#10 +
           '  3. Add "AI MIDI Composer" to a MIDI track' + #13#10 +
           '  4. First use downloads the AI model (~4 GB)';
    MsgBox(Msg, mbInformation, MB_OK);
  end;
end;
