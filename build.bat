@echo off
setlocal EnableDelayedExpansion
title AI MIDI Composer - Build VST3
cd /d "%~dp0"

echo.
echo  ============================================================
echo   AI MIDI Composer - VST3 Build (clean)
echo  ============================================================
echo.

:: =============================================================================
:: 0. NUKE previous C++ build artifacts (per user request: fresh rebuild)
:: =============================================================================
if exist build (
    echo  [..] Removing previous build\ folder...
    rmdir /s /q build
)
if exist dist\AIMidiComposer-Installer.exe (
    echo  [..] Removing previous installer .exe...
    del /q dist\AIMidiComposer-Installer.exe
)
:: Do NOT touch sidecar\.venv or sidecar\dist - use clean.bat for that.

:: =============================================================================
:: 1. Load Visual Studio environment via vswhere.exe
:: =============================================================================
where cl >nul 2>&1
if errorlevel 1 (
    echo  [..] cl.exe not on PATH - locating Visual Studio via vswhere...

    set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo  [ERROR] vswhere.exe not found at: !VSWHERE!
        pause
        exit /b 1
    )

    set "VSROOT="
    for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSROOT=%%I"

    if "!VSROOT!"=="" (
        echo  [ERROR] vswhere found no VS install with C++ tools.
        pause
        exit /b 1
    )
    echo  [OK] VS installation: !VSROOT!

    set "VCVARS=!VSROOT!\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" (
        echo  [ERROR] vcvars64.bat not found at: !VCVARS!
        pause
        exit /b 1
    )
    echo  [OK] Loading: !VCVARS!
    call "!VCVARS!" >nul
    if errorlevel 1 ( echo  [ERROR] vcvars64.bat failed. & pause & exit /b 1 )
) else (
    echo  [OK] MSVC environment already loaded.
)

:: =============================================================================
:: 2. Ensure Ninja is available
:: =============================================================================
where ninja >nul 2>&1
if errorlevel 1 (
    if exist "tools\ninja.exe" (
        set "PATH=%CD%\tools;!PATH!"
        echo  [OK] Using local tools\ninja.exe
    ) else (
        echo  [..] Ninja not found - downloading from github.com/ninja-build...
        if not exist tools mkdir tools
        set "NINJA_URL=https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip"
        set "NINJA_ZIP=tools\ninja-win.zip"

        powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '!NINJA_URL!' -OutFile '!NINJA_ZIP!'"
        if errorlevel 1 ( echo  [ERROR] Failed to download Ninja. & pause & exit /b 1 )

        powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Path '!NINJA_ZIP!' -DestinationPath 'tools' -Force"
        if errorlevel 1 ( echo  [ERROR] Failed to extract Ninja zip. & pause & exit /b 1 )

        del /q "!NINJA_ZIP!" 2>nul

        if not exist "tools\ninja.exe" ( echo  [ERROR] ninja.exe missing. & pause & exit /b 1 )
        set "PATH=%CD%\tools;!PATH!"
        echo  [OK] Ninja installed to: %CD%\tools\ninja.exe
    )
) else (
    echo  [OK] Ninja found on PATH.
)

:: =============================================================================
:: 3. CMake configure (always fresh after the nuke above)
:: =============================================================================
echo.
echo  [..] Configuring with CMake...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 ( echo  [ERROR] CMake configure failed. & pause & exit /b 1 )

:: =============================================================================
:: 4. Build (verbose)
:: =============================================================================
echo.
echo  ============================================================
echo   Compiling...
echo   Linking (with LTO) can take several minutes - be patient.
echo  ============================================================
echo.

set "BUILD_T0=%TIME%"

cmake --build build --config Release --parallel -v
set "BUILD_ERR=%ERRORLEVEL%"

if NOT "!BUILD_ERR!"=="0" (
    echo.
    echo  [ERROR] Build failed. cmake --build returned !BUILD_ERR!
    pause
    exit /b !BUILD_ERR!
)

:: =============================================================================
:: 5. Verify artifact
:: =============================================================================
set "VST3_PATH=build\AIMidiComposerVST_artefacts\Release\VST3\AI MIDI Composer.vst3\Contents\x86_64-win\AI MIDI Composer.vst3"
if not exist "!VST3_PATH!" (
    echo.
    echo  [ERROR] Build reported success but VST3 is missing:
    echo          !VST3_PATH!
    pause
    exit /b 1
)

for %%F in ("!VST3_PATH!") do set "VST3_SIZE=%%~zF"
if "!VST3_SIZE!"=="0" ( echo  [ERROR] VST3 file is zero bytes. & pause & exit /b 1 )

echo.
echo  ============================================================
echo   [OK] Build complete.
echo  ============================================================
echo   Started:  !BUILD_T0!
echo   Finished: !TIME!
echo   VST3:     !VST3_PATH!
echo   Size:     !VST3_SIZE! bytes
echo.
echo   Next: run build_sidecar.bat to freeze the Python inference server.
echo.
pause
