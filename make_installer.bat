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
:: Search common install locations for Inno Setup 6
for %%P in (
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    "C:\Program Files\Inno Setup 6\ISCC.exe"
    "C:\Program Files (x86)\Inno Setup 6.7.1\ISCC.exe"
    "C:\Program Files\Inno Setup 6.7.1\ISCC.exe"
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
) do (
    if "!ISCC!"=="" if exist %%P set "ISCC=%%~P"
)

:: Try registry lookup as fallback
if "!ISCC!"=="" (
    for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" /v "InstallLocation" 2^>nul') do (
        if exist "%%B\ISCC.exe" set "ISCC=%%B\ISCC.exe"
    )
)
if "!ISCC!"=="" (
    for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" /v "InstallLocation" 2^>nul') do (
        if exist "%%B\ISCC.exe" set "ISCC=%%B\ISCC.exe"
    )
)

:: Last resort: search PATH
if "!ISCC!"=="" (
    for /f "delims=" %%F in ('where ISCC.exe 2^>nul') do (
        if "!ISCC!"=="" set "ISCC=%%F"
    )
)

if "!ISCC!"=="" (
    echo  [ERROR] Inno Setup 6 not found.
    echo          Searched common paths and registry.
    echo          Download from: https://jrsoftware.org/isdl.php
    echo          Install to default location and retry.
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
echo   dist\AIMidiComposer-Installer.exe  (run this to install)
echo   dist\AIMidiComposer-Installer-1.bin  (keep alongside .exe)
echo   NOTE: Keep all dist\ files together when distributing.
echo.
for %%F in ("dist\AIMidiComposer-Installer.exe") do echo   Setup.exe : %%~zF bytes
for %%F in ("dist\AIMidiComposer-Installer-1.bin") do echo   Data .bin : %%~zF bytes
echo.
pause
