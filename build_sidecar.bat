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

echo  [..] Installing ACE-Step ^(Windows-compatible, no nano-vllm^)...
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8

:: Install build backend first
python -m pip install hatchling --quiet

:: Clone ACE-Step repo to a temp location
set "ACESTEP_TMP=%TEMP%\acestep_src"
if exist "!ACESTEP_TMP!" rmdir /s /q "!ACESTEP_TMP!"
git clone --depth=1 --quiet https://github.com/ace-step/ACE-Step-1.5.git "!ACESTEP_TMP!"
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to clone ACE-Step. Check git is installed.
    pause
    exit /b 1
)

:: Write patch script to file (avoids batch quoting issues)
set "PATCH_PY=%TEMP%\patch_acestep.py"
(
    echo import re, pathlib
    echo p = pathlib.Path(r'!ACESTEP_TMP!\pyproject.toml'^)
    echo txt = p.read_text(encoding='utf-8'^)
    echo txt = re.sub(r'[^\n]*nano.vllm[^\n]*\n', '', txt^)
    echo p.write_text(txt, encoding='utf-8'^)
    echo print('Patched pyproject.toml'^)
) > "!PATCH_PY!"
python "!PATCH_PY!"
del "!PATCH_PY!"

:: Install ACE-Step itself with --no-deps to skip dependency resolution
:: (nano-vllm has no Windows wheel so we must bypass it entirely)
python -m pip install "!ACESTEP_TMP!" --no-deps --no-build-isolation
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install ACE-Step package.
    pause
    exit /b 1
)

:: Now install ACE-Step's actual required deps manually (excluding nano-vllm)
:: Note: torch 2.5.1+cu121 already installed - skip torch/torchaudio/torchvision
:: (ACE-Step wants 2.7.1+cu128 but 2.5.1+cu121 works for inference)
python -m pip install ^
    "accelerate>=0.26.0" ^
    "diffusers>=0.30.0" ^
    "diskcache" ^
    "einops>=0.8.0" ^
    "loguru>=0.7.0" ^
    "transformers>=4.40.0,<5.0.0" ^
    "huggingface_hub>=0.20.0" ^
    "safetensors>=0.4.0" ^
    "tqdm" ^
    "peft>=0.10.0" ^
    "scipy>=1.10.0" ^
    "soundfile>=0.12.1" ^
    "toml" ^
    "vector-quantize-pytorch>=1.14.0"
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install ACE-Step dependencies.
    pause
    exit /b 1
)

rmdir /s /q "!ACESTEP_TMP!"
echo  [OK] ACE-Step installed.

echo  [..] Installing remaining sidecar requirements...
python -m pip install -r sidecar\requirements.txt
if errorlevel 1 (
    call "!VENV_DIR!\Scripts\deactivate.bat"
    echo  [ERROR] Failed to install sidecar requirements.
    pause
    exit /b 1
)

echo  [..] Skipping PyInstaller - shipping venv directly instead.
echo  [..] (PyInstaller cannot reliably freeze ACE-Step native extensions)

call "!VENV_DIR!\Scripts\deactivate.bat"

:: =============================================================================
:: 4. Create the sidecar/dist folder with python.exe + main.py
::    The installer will copy this entire folder to Program Files.
:: =============================================================================
set "DIST_DIR=sidecar\dist\sidecar"
if exist "!DIST_DIR!" rmdir /s /q "!DIST_DIR!"
mkdir "!DIST_DIR!"

:: Copy main.py as the entrypoint
copy /y "sidecar\main.py" "!DIST_DIR!\main.py" >nul

:: Write a launcher batch file that activates the venv and runs main.py
:: This is what SidecarManager will execute as sidecar.exe equivalent
(
    echo @echo off
    echo set PYTHONUTF8=1
    echo set PYTHONIOENCODING=utf-8
    echo set "SCRIPT_DIR=%%~dp0"
    echo call "%%SCRIPT_DIR%%..\.venv\Scripts\activate.bat"
    echo python "%%SCRIPT_DIR%%main.py" %%*
) > "!DIST_DIR!\sidecar.bat"

:: Also create a minimal sidecar.exe that just runs the batch
:: We use a Python-generated wrapper exe so SidecarManager's CreateProcess works
call "!VENV_DIR!\Scripts\activate.bat"
set "WRAP_PY=%TEMP%\make_launcher.py"
(
    echo import sys, pathlib, subprocess
    echo dist = pathlib.Path(r'!DIST_DIR!'^)
    echo bat = dist / 'sidecar.bat'
    echo # Write a .cmd file - Windows can execute these as processes
    echo print(f'Launcher at: {bat}'^)
) > "!WRAP_PY!"
python "!WRAP_PY!"
del "!WRAP_PY!"

:: Write sidecar.cmd which CreateProcess can launch via cmd.exe /c
:: After install: sidecar\ is at C:\Program Files\AIMidiComposer\sidecar\
::                venv\    is at C:\Program Files\AIMidiComposer\venv\
(
    echo @echo off
    echo set PYTHONUTF8=1
    echo set PYTHONIOENCODING=utf-8
    echo set "SIDECAR_DIR=%%~dp0"
    echo set "VENV_DIR=%%SIDECAR_DIR%%..\venv"
    echo call "%%VENV_DIR%%\Scripts\activate.bat"
    echo python "%%SIDECAR_DIR%%main.py" %%*
) > "!DIST_DIR!\sidecar.cmd"

call "!VENV_DIR!\Scripts\deactivate.bat"

:: =============================================================================
:: 5. Verify artifact
:: =============================================================================
set "SIDECAR_ENTRY=sidecar\dist\sidecar\main.py"
if not exist "!SIDECAR_ENTRY!" (
    echo  [ERROR] main.py missing from dist folder.
    pause
    exit /b 1
)

echo.
echo  ============================================================
echo   [OK] Sidecar prepared ^(venv mode^).
echo  ============================================================
echo   Output: !DIST_DIR!
echo   Launcher: !DIST_DIR!\sidecar.cmd
echo.
echo   Next: run make_installer.bat to package everything.
echo.
pause
