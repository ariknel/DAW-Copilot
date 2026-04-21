@echo off
setlocal EnableDelayedExpansion
title AI MIDI Composer - Build Installer
cd /d "%~dp0"

echo.
echo  ============================================================
echo   AI MIDI Composer - Installer Builder (Inno Setup 5)
echo  ============================================================
echo.

set "VST3=build\AIMidiComposerVST_artefacts\Release\VST3\AI MIDI Composer.vst3"
if not exist "!VST3!" (
    echo  [ERROR] VST3 not found. Run build.bat first.
    pause & exit /b 1
)
echo  [OK] VST3 found.

set "SIDECAR=sidecar\dist\sidecar\sidecar.cmd"
if not exist "!SIDECAR!" (
    echo  [ERROR] Sidecar not found. Run build_sidecar.bat first.
    pause & exit /b 1
)
echo  [OK] Sidecar found.

:: Check for zip tool (use PowerShell - always available on Win10+)
echo  [..] Zipping venv into single file (avoids Defender scanning 50k files)...
if exist "sidecar\dist\venv.zip" del /q "sidecar\dist\venv.zip"
if not exist "sidecar\dist" mkdir "sidecar\dist"
powershell -NoProfile -Command ^
    "Compress-Archive -Path 'sidecar\.venv\*' -DestinationPath 'sidecar\dist\venv.zip' -Force"
if errorlevel 1 (
    echo  [ERROR] Failed to zip venv. Check PowerShell is available.
    pause & exit /b 1
)
for %%F in ("sidecar\dist\venv.zip") do echo  [OK] venv.zip: %%~zF bytes

set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 5\ISCC.exe" set "ISCC=C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
if exist "C:\Program Files\Inno Setup 5\ISCC.exe"       set "ISCC=C:\Program Files\Inno Setup 5\ISCC.exe"

if "!ISCC!"=="" (
    echo  [ERROR] Inno Setup 5 not found.
    echo          Download: https://jrsoftware.org/isdl.php
    pause & exit /b 1
)
echo  [OK] Inno Setup: !ISCC!

if not exist dist mkdir dist
if exist dist\AIMidiComposer-Installer.exe del /q dist\AIMidiComposer-Installer.exe

echo.
echo  [..] Building installer...
echo.
"!ISCC!" installer.iss
if errorlevel 1 (
    echo  [ERROR] Build failed.
    pause & exit /b 1
)

if not exist dist\AIMidiComposer-Installer.exe (
    echo  [ERROR] installer .exe missing after build.
    pause & exit /b 1
)

echo.
echo  ============================================================
echo   SUCCESS: dist\AIMidiComposer-Installer.exe
echo  ============================================================
echo.
for %%F in ("dist\AIMidiComposer-Installer.exe") do echo   Size: %%~zF bytes
echo.
pause
