; AI MIDI Composer - Inno Setup 5 Script
; Pure ASCII only - no Unicode anywhere
;
; Layout:
;   VST3 DLL:  C:\Program Files\Common Files\VST3\AI MIDI Composer.vst3\
;   Sidecar:   C:\Program Files\AIMidiComposer\sidecar\
;
; IMPORTANT: The sidecar is NOT inside the .vst3 folder.
; Bundling 1.5 GB of Python DLLs inside the .vst3 folder causes DAW
; plugin scanners to hang because Windows Defender scans every file
; in the folder during VST3 discovery. Keeping it separate avoids this.

#define AppName     "AI MIDI Composer"
#define AppVersion  "0.1.0"
#define AppPublisher "Arik"
#define VST3Name    "AI MIDI Composer.vst3"
#define SidecarDir  "{pf}\AIMidiComposer\sidecar"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppId={{8a7f4b4c-3e5d-4e6a-9b12-21c9a7f58ab0}

; 64-bit install so {pf} = C:\Program Files (not x86)
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
; Wipe previous installs before copying new files (prevents Error 183)
Type: filesandordirs; Name: "{pf}\Common Files\VST3\{#VST3Name}"
Type: filesandordirs; Name: "{pf}\AIMidiComposer\sidecar"

[Files]
; VST3 plugin DLL - goes in the standard VST3 folder (small, just the DLL)
Source: "build\AIMidiComposerVST_artefacts\Release\VST3\{#VST3Name}\*"; \
    DestDir: "{pf}\Common Files\VST3\{#VST3Name}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Sidecar - goes in a SEPARATE folder, NOT inside the .vst3 bundle.
; This prevents DAW scanner hangs from Defender scanning 1.5 GB of DLLs.
Source: "sidecar\dist\sidecar\*"; \
    DestDir: "{#SidecarDir}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; README
Source: "docs\README.txt"; \
    DestDir: "{pf}\AIMidiComposer"; \
    Flags: ignoreversion

[Run]
; Firewall rule for sidecar localhost binding
Filename: "netsh"; \
    Parameters: "advfirewall firewall add rule name=""AI MIDI Composer Sidecar"" dir=in action=allow program=""{#SidecarDir}\sidecar.exe"" enable=yes profile=any"; \
    Flags: runhidden; StatusMsg: "Configuring Windows Firewall..."

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
           'VST3 plugin:' + #13#10 +
           '   C:\Program Files\Common Files\VST3\{#VST3Name}' + #13#10 + #13#10 +
           'Inference engine:' + #13#10 +
           '   C:\Program Files\AIMidiComposer\sidecar\' + #13#10 + #13#10 +
           'Next steps:' + #13#10 +
           '   1. Open your DAW' + #13#10 +
           '   2. Settings > Plugins > Rescan VST3' + #13#10 +
           '   3. Add "AI MIDI Composer" to a MIDI track' + #13#10 +
           '   4. On first use, the AI model (~3 GB) will download';
    MsgBox(Msg, mbInformation, MB_OK);
  end;
end;
