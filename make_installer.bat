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
    echo  [ERROR] VST3 not found.
    echo          Run build.bat first.
    pause & exit /b 1
)
echo  [OK] VST3 found.

set "SIDECAR=sidecar\dist\sidecar\sidecar.exe"
if not exist "!SIDECAR!" (
    echo  [ERROR] Sidecar not found.
    echo          Run build_sidecar.bat first.
    pause & exit /b 1
)
echo  [OK] Sidecar found.

set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 5\ISCC.exe" set "ISCC=C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
if exist "C:\Program Files\Inno Setup 5\ISCC.exe"       set "ISCC=C:\Program Files\Inno Setup 5\ISCC.exe"

if "!ISCC!"=="" (
    echo  [ERROR] Inno Setup 5 not found.
    echo          Download from: https://jrsoftware.org/isdl.php
    echo          Install it then re-run this script.
    pause & exit /b 1
)
echo  [OK] Inno Setup: !ISCC!

if not exist dist mkdir dist

:: Always delete any previous installer first so a stale one can't
:: accidentally get reused in testing.
if exist dist\AIMidiComposer-Installer.exe (
    echo  [..] Removing previous installer .exe...
    del /q dist\AIMidiComposer-Installer.exe
)

echo.
echo  [..] Building installer...
echo.
"!ISCC!" installer.iss
if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed. See errors above.
    pause & exit /b 1
)

if not exist dist\AIMidiComposer-Installer.exe (
    echo  [ERROR] ISCC reported success but installer .exe is missing.
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
