@echo off
setlocal EnableDelayedExpansion
title AI MIDI Composer - FULL Clean
cd /d "%~dp0"

echo.
echo  ============================================================
echo   AI MIDI Composer - FULL Clean
echo  ============================================================
echo.
echo  This removes EVERYTHING rebuilt by build.bat AND build_sidecar.bat:
echo    - build\                     ^(C++ intermediates^)
echo    - dist\                      ^(installer output^)
echo    - tools\                     ^(Ninja download^)
echo    - sidecar\.venv\             ^(~10 GB Python packages^)
echo    - sidecar\build\             ^(PyInstaller intermediates^)
echo    - sidecar\dist\              ^(PyInstaller output, ~1.5 GB^)
echo.
echo  After this, a full rebuild takes ~30 minutes and redownloads
echo  torch (~2.4 GB). Only do this if you are sure.
echo.
set /p CONFIRM="Type YES to confirm: "
if NOT "!CONFIRM!"=="YES" ( echo  Cancelled. & pause & exit /b 0 )

if exist build (
    echo  [..] Removing build\...
    rmdir /s /q build
)
if exist dist (
    echo  [..] Removing dist\...
    rmdir /s /q dist
)
if exist tools (
    echo  [..] Removing tools\...
    rmdir /s /q tools
)
if exist sidecar\.venv (
    echo  [..] Removing sidecar\.venv\ (this may take a minute)...
    rmdir /s /q sidecar\.venv
)
if exist sidecar\build (
    echo  [..] Removing sidecar\build\...
    rmdir /s /q sidecar\build
)
if exist sidecar\dist (
    echo  [..] Removing sidecar\dist\...
    rmdir /s /q sidecar\dist
)

echo.
echo  [OK] Clean complete. Run build.bat + build_sidecar.bat + make_installer.bat.
echo.
pause
