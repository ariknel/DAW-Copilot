# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for AI MIDI Composer v2 sidecar (ACE-Step 1.5 + Basic Pitch)

import sys, os
from PyInstaller.utils.hooks import collect_data_files, collect_submodules, collect_all

block_cipher = None
hiddenimports = []
datas = []
binaries = []

# FastAPI / uvicorn
hiddenimports += collect_submodules("fastapi")
hiddenimports += collect_submodules("uvicorn")
hiddenimports += collect_submodules("starlette")
hiddenimports += ["anyio", "anyio.backends.asyncio", "anyio.backends.trio"]

# ACE-Step - installed from GitHub, package may be 'ace_step' or 'acestep'
# Try both names with collect_all (grabs submodules + data + binaries)
for pkg in ("ace_step", "acestep"):
    try:
        d, b, h = collect_all(pkg)
        datas += d; binaries += b; hiddenimports += h
        print(f"[spec] collected all from: {pkg}")
    except Exception as e:
        print(f"[spec] {pkg} not found: {e}")

# ACE-Step explicit submodule list (belt-and-suspenders)
for mod in [
    "acestep.pipeline", "acestep.models", "acestep.inference",
    "ace_step.pipeline", "ace_step.models", "ace_step.inference",
    "diffusers", "accelerate", "transformers",
    "einops", "loguru", "diskcache",
]:
    try:
        hiddenimports += collect_submodules(mod)
    except Exception:
        hiddenimports.append(mod)

# Basic Pitch
try:
    d, b, h = collect_all("basic_pitch")
    datas += d; binaries += b; hiddenimports += h
except Exception as e:
    print(f"[spec] basic_pitch: {e}")

# Audio libs
for pkg in ("soundfile", "librosa", "audioread", "resampy", "soxr"):
    try:
        hiddenimports += collect_submodules(pkg)
        datas += collect_data_files(pkg)
    except Exception:
        pass

# HuggingFace
try:
    hiddenimports += collect_submodules("huggingface_hub")
    datas += collect_data_files("huggingface_hub")
except Exception:
    pass

# Diffusers / transformers / accelerate
for pkg in ("diffusers", "transformers", "accelerate"):
    try:
        hiddenimports += collect_submodules(pkg)
    except Exception:
        pass

# Torch (don't collect_all - too large; just mark as hidden)
hiddenimports += ["torch", "torchaudio"]

# Misc
hiddenimports += ["psutil", "numpy", "scipy", "soundfile", "packaging",
                  "safetensors", "huggingface_hub"]

a = Analysis(
    ["main.py"],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=["rthook_syspath.py"],
    excludes=["tkinter", "PIL", "cv2", "PyQt5", "wx",
              "gradio", "lightning", "modelscope", "lycoris_lora", "matplotlib"],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz, a.scripts, [],
    exclude_binaries=True,
    name="sidecar",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe, a.binaries, a.zipfiles, a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="sidecar",
)
