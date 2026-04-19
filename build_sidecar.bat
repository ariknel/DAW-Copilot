@echo off
setlocal EnableDelayedExpansion
title AI MIDI Composer - Build Sidecar
cd /d "%~dp0"

echo.
echo  ============================================================
echo   Building AI MIDI Composer sidecar (PyInstaller)
echo  ============================================================
echo.

:: =============================================================================
:: 1. Locate a COMPATIBLE Python (3.10, 3.11, or 3.12).
::    Five-layer detection so at least one always finds your install.
:: =============================================================================
set "PY_CMD="
set "PY_VER="

echo  [..] Searching for Python 3.10, 3.11, or 3.12...

:: --- LAYER 1: py launcher with explicit version ---
for %%V in (3.11 3.12 3.10) do (
    if "!PY_CMD!"=="" (
        py -%%V -c "import sys; sys.exit(0)" >nul 2>&1
        if !errorlevel! equ 0 (
            set "PY_CMD=py -%%V"
            set "PY_VER=%%V"
            echo  [OK] Found via py launcher: py -%%V
        )
    )
)

:: --- LAYER 2: versioned python.exe on PATH ---
if "!PY_CMD!"=="" (
    for %%V in (3.11 3.12 3.10) do (
        if "!PY_CMD!"=="" (
            where python%%V >nul 2>&1
            if !errorlevel! equ 0 (
                set "PY_CMD=python%%V"
                set "PY_VER=%%V"
                echo  [OK] Found via PATH: python%%V
            )
        )
    )
)

:: --- LAYER 3: plain python on PATH, if its version matches ---
if "!PY_CMD!"=="" (
    for %%C in (python python3) do (
        if "!PY_CMD!"=="" (
            where %%C >nul 2>&1
            if !errorlevel! equ 0 (
                for /f "tokens=2" %%A in ('%%C --version 2^>^&1') do (
                    for /f "tokens=1,2 delims=." %%M in ("%%A") do (
                        if "%%M.%%N"=="3.10" ( set "PY_CMD=%%C" & set "PY_VER=3.10" )
                        if "%%M.%%N"=="3.11" ( set "PY_CMD=%%C" & set "PY_VER=3.11" )
                        if "%%M.%%N"=="3.12" ( set "PY_CMD=%%C" & set "PY_VER=3.12" )
                    )
                )
                if not "!PY_CMD!"=="" echo  [OK] Found via PATH: %%C  (version !PY_VER!)
            )
        )
    )
)

:: --- LAYER 4: scan standard install locations directly ---
if "!PY_CMD!"=="" (
    for %%V in (311 312 310) do (
        if "!PY_CMD!"=="" (
            for %%P in (
                "C:\Python%%V\python.exe"
                "C:\Program Files\Python%%V\python.exe"
                "C:\Program Files (x86)\Python%%V\python.exe"
                "%LOCALAPPDATA%\Programs\Python\Python%%V\python.exe"
                "%USERPROFILE%\AppData\Local\Programs\Python\Python%%V\python.exe"
            ) do (
                if "!PY_CMD!"=="" (
                    if exist %%P (
                        set "PY_CMD=%%P"
                        if "%%V"=="310" set "PY_VER=3.10"
                        if "%%V"=="311" set "PY_VER=3.11"
                        if "%%V"=="312" set "PY_VER=3.12"
                        echo  [OK] Found via filesystem scan: %%P
                    )
                )
            )
        )
    )
)

:: --- LAYER 5: Windows registry ---
if "!PY_CMD!"=="" (
    for %%V in (3.11 3.12 3.10) do (
        if "!PY_CMD!"=="" (
            for /f "usebackq tokens=2*" %%A in (`reg query "HKCU\Software\Python\PythonCore\%%V\InstallPath" /ve 2^>nul`) do (
                if exist "%%BPython.exe" (
                    set "PY_CMD=%%BPython.exe"
                    set "PY_VER=%%V"
                    echo  [OK] Found via registry: %%B
                )
            )
            if "!PY_CMD!"=="" (
                for /f "usebackq tokens=2*" %%A in (`reg query "HKLM\Software\Python\PythonCore\%%V\InstallPath" /ve 2^>nul`) do (
                    if exist "%%BPython.exe" (
                        set "PY_CMD=%%BPython.exe"
                        set "PY_VER=%%V"
                        echo  [OK] Found via HKLM registry: %%B
                    )
                )
            )
        )
    )
)

if "!PY_CMD!"=="" (
    echo.
    echo  [ERROR] Could not find Python 3.10, 3.11, or 3.12 after searching:
    echo            1. py launcher  ^(py -3.11, etc.^)
    echo            2. Versioned exe on PATH  ^(python3.11.exe, etc.^)
    echo            3. Plain python/python3 on PATH  ^(version-checked^)
    echo            4. Standard install paths  ^(C:\Python311\, Programs\Python\, etc.^)
    echo            5. Windows registry  ^(HKCU/HKLM\Software\Python\PythonCore^)
    echo.
    echo          Diagnostics: run these two commands in a fresh cmd and paste output:
    echo            py -0
    echo            where python
    echo.
    pause
    exit /b 1
)

echo  [OK] Python command: !PY_CMD!  ^(version !PY_VER!.x^)

:: Sanity check: make sure the command actually runs.
:: If PY_CMD contains spaces (e.g. "py -3.11"), call unquoted so args parse.
:: If PY_CMD is a path (e.g. "C:\Python311\python.exe"), it has no args so
:: unquoted-with-percent-substitution still works.
!PY_CMD! --version >nul 2>&1
if errorlevel 1 (
    echo  [ERROR] Selected Python command does not execute: !PY_CMD!
    pause
    exit /b 1
)

:: =============================================================================
:: 2. Create / validate venv
:: =============================================================================
set "VENV_DIR=sidecar\.venv"
set "VENV_PY=!VENV_DIR!\Scripts\python.exe"
set "RECREATE_VENV=0"

if exist "!VENV_DIR!" (
    for /f "usebackq tokens=2" %%A in (`"!VENV_PY!" --version 2^>^&1`) do set "EXISTING_VER=%%A"
    for /f "tokens=1,2 delims=." %%A in ("!EXISTING_VER!") do set "EXISTING_MM=%%A.%%B"
    echo  [..] Existing venv uses Python !EXISTING_MM!
    if NOT "!EXISTING_MM!"=="!PY_VER!" (
        echo  [..] Venv Python version mismatch - recreating venv...
        set "RECREATE_VENV=1"
    )
)

if "!RECREATE_VENV!"=="1" rmdir /s /q "!VENV_DIR!"

if not exist "!VENV_DIR!" (
    echo  [..] Creating venv with Python !PY_VER!...
    pushd sidecar
    !PY_CMD! -m venv .venv
    if errorlevel 1 (
        popd
        echo  [ERROR] Failed to create venv.
        pause
        exit /b 1
    )
    popd
)

:: =============================================================================
:: 3. Activate venv and install dependencies
:: =============================================================================
echo  [..] Activating venv...
call "!VENV_DIR!\Scripts\activate.bat"

echo  [..] Upgrading pip...
python -m pip install --upgrade pip

echo.
echo  Pick a PyTorch wheel:
echo     1) CUDA 12.1 (NVIDIA, recommended for speed)
echo     2) CPU only  (slower, broadest compatibility)
set /p TORCH_CHOICE="  Enter 1 or 2: "
if "!TORCH_CHOICE!"=="1" (
    echo  [..] Installing CUDA 12.1 torch...
    python -m pip install torch --index-url https://download.pytorch.org/whl/cu121
) else (
    echo  [..] Installing CPU-only torch...
    python -m pip install torch
)
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install torch.
    pause
    exit /b 1
)

echo  [..] Installing sidecar requirements...
python -m pip install -r sidecar\requirements.txt
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install sidecar requirements.
    pause
    exit /b 1
)

echo  [..] Installing PyInstaller...
python -m pip install pyinstaller==6.10.0
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install PyInstaller.
    pause
    exit /b 1
)

:: =============================================================================
:: 4. Freeze the sidecar into an .exe via PyInstaller
:: =============================================================================
pushd sidecar
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
echo.
echo  [..] Running PyInstaller (this takes several minutes)...
pyinstaller sidecar.spec --clean --noconfirm
if errorlevel 1 (
    popd
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] PyInstaller failed.
    pause
    exit /b 1
)
popd

call "!VENV_DIR!\Scripts\deactivate.bat"

:: =============================================================================
:: 5. Verify artifact
:: =============================================================================
set "SIDECAR_EXE=sidecar\dist\sidecar\sidecar.exe"
if not exist "!SIDECAR_EXE!" (
    echo  [ERROR] sidecar.exe missing after PyInstaller run.
    pause
    exit /b 1
)

echo.
echo  ============================================================
echo   [OK] Sidecar built.
echo  ============================================================
echo   Output: !SIDECAR_EXE!
echo.
echo   Next: run make_installer.bat to package everything.
echo.
pause
