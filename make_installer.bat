@echo off
setlocal EnableDelayedExpansion
title AI MIDI Composer - Build Installer
cd /d "%~dp0"

echo.
echo  ============================================================
echo   AI MIDI Composer - Installer Builder
echo  ============================================================
echo.
echo  Working dir: %CD%
echo.

if not exist "build\AIMidiComposerVST_artefacts\Release\VST3\AI MIDI Composer.vst3" (
    echo  [ERROR] VST3 not found. Run build.bat first.
    pause & exit /b 1
)
echo  [OK] VST3 found.

if not exist "sidecar\dist\sidecar\sidecar.cmd" (
    echo  [ERROR] sidecar.cmd not found. Run build_sidecar.bat first.
    pause & exit /b 1
)
echo  [OK] Sidecar found.

if not exist "sidecar\.venv\Scripts\python.exe" (
    echo  [ERROR] venv not found. Run build_sidecar.bat first.
    pause & exit /b 1
)
echo  [OK] Venv found.

echo.
echo  [..] Zipping venv into single archive (takes 3-5 min)...
echo.
if not exist "sidecar\dist" mkdir "sidecar\dist"
python installer\split_venv.py
if errorlevel 1 (
    echo  [ERROR] Venv zip failed.
    pause & exit /b 1
)

if not exist "sidecar\dist\venv.zip" (
    echo  [ERROR] venv.zip was not created.
    pause & exit /b 1
)
for %%F in ("sidecar\dist\venv.zip") do echo  [OK] venv.zip  %%~zF bytes

set "ISCC="
for %%P in (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    "C:\Program Files\Inno Setup 6\ISCC.exe"
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
    "C:\Program Files\Inno Setup 5\ISCC.exe"
) do (
    if "!ISCC!"=="" if exist %%P set "ISCC=%%~P"
)

if "!ISCC!"=="" (
    echo  [ERROR] Inno Setup not found. Download: https://jrsoftware.org/isdl.php
    pause & exit /b 1
)
echo  [OK] Inno Setup: !ISCC!

if not exist dist mkdir dist
if exist "dist\AIMidiComposer-Installer.exe" del /q "dist\AIMidiComposer-Installer.exe"

echo.
echo  [..] Running Inno Setup...
echo.
"!ISCC!" installer.iss
if errorlevel 1 (
    echo  [ERROR] Inno Setup failed.
    pause & exit /b 1
)

if not exist "dist\AIMidiComposer-Installer.exe" (
    echo  [ERROR] Installer exe missing.
    pause & exit /b 1
)

echo.
echo  ============================================================
echo   SUCCESS
echo  ============================================================
for %%F in ("dist\AIMidiComposer-Installer.exe") do echo   Size: %%~zF bytes
echo.
pause
