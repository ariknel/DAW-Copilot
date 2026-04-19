# PyInstaller spec for the AI MIDI Composer sidecar.
# Build with:  pyinstaller sidecar.spec --clean --noconfirm
#
# Strategy: MINIMAL manual collection.
# torch, numpy, transformers, tokenizers, safetensors all ship their own
# PyInstaller hooks inside the package. Let those hooks run automatically.
# We only manually collect packages that LACK built-in hooks.

# -*- mode: python ; coding: utf-8 -*-
from PyInstaller.utils.hooks import collect_all, collect_data_files

datas    = []
binaries = []
hiddenimports = []

# anticipation: git-installed, no PyInstaller hook, collect fully (small pkg)
try:
    d, b, h = collect_all("anticipation")
    datas += d; binaries += b; hiddenimports += h
except Exception as e:
    print(f"[spec] anticipation collect failed: {e}")

# huggingface_hub: needs its metadata/data files for model download to work
try:
    datas += collect_data_files("huggingface_hub")
except Exception as e:
    print(f"[spec] huggingface_hub data failed: {e}")

# mido: pure Python, tiny, no hook - just list the submodules explicitly
hiddenimports += [
    "mido",
    "mido.backends",
    "mido.backends.backend",
    "mido.messages",
    "mido.midifiles",
    "mido.midifiles.midifiles",
    "mido.midifiles.meta",
    "mido.midifiles.tracks",
    "mido.frozen",
    "mido.ports",
]

# uvicorn: dynamic imports not caught by static analysis
hiddenimports += [
    "uvicorn",
    "uvicorn.logging",
    "uvicorn.loops.auto",
    "uvicorn.loops.asyncio",
    "uvicorn.protocols.http.auto",
    "uvicorn.protocols.http.h11_impl",
    "uvicorn.protocols.websockets.auto",
    "uvicorn.protocols.websockets.wsproto_impl",
    "uvicorn.lifespan.on",
    "uvicorn.lifespan.off",
]

# fastapi/starlette: routers loaded dynamically
hiddenimports += [
    "fastapi",
    "fastapi.responses",
    "fastapi.routing",
    "starlette.routing",
    "starlette.middleware.cors",
    "starlette.responses",
    "starlette.staticfiles",
    "anyio",
    "anyio._backends._asyncio",
    "sniffio",
    "h11",
    "click",
]

# psutil: only the Windows backend is needed
hiddenimports += [
    "psutil",
    "psutil._pswindows",
    "psutil._psutil_windows",
]

# numpy._core C-extensions that PyInstaller misses via static analysis.
# These are loaded dynamically inside numpy/__init__.py and are NOT
# caught by the built-in numpy hook in older PyInstaller versions.
hiddenimports += [
    "numpy._core._exceptions",
    "numpy._core._multiarray_umath",
    "numpy._core._multiarray_tests",
    "numpy._core.multiarray",
    "numpy._core.umath",
    "numpy._core._operand_flag_tests",
    "numpy._core._rational_tests",
    "numpy._core._simd",
    "numpy._core._struct_ufunc_tests",
    "numpy._core._umath_tests",
    "numpy._core.strings",
    "numpy.random._pickle",
    "numpy.random._common",
    "numpy.random._bounded_integers",
    "numpy.random._mt19937",
    "numpy.random._philox",
    "numpy.random._pcg64",
    "numpy.random._sfc64",
    "numpy.random._generator",
]

# unittest.mock is imported by torch._dispatch.python at module load time.
# Even though we excluded unittest tests, the stdlib unittest package itself
# must be present. PyInstaller normally includes stdlib but excludes can
# sometimes strip it. Be explicit.
hiddenimports += [
    "unittest",
    "unittest.mock",
    "unittest.case",
]

a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=['rthook_syspath.py'],
    excludes=[
        # Heavy libs we never use
        'matplotlib', 'pandas', 'PIL', 'Pillow', 'scipy',
        'sklearn', 'sympy', 'cv2',
        # Notebook / IDE
        'notebook', 'jupyter', 'IPython', 'ipykernel',
        # GUI toolkits
        'tkinter', 'PyQt5', 'PyQt6', 'PySide2', 'PySide6', 'wx',
        # Test frameworks (keep unittest — torch._dispatch imports unittest.mock)
        'pytest',
        # pydantic v1 legacy layer (huge, we use pydantic v2)
        'pydantic.v1',
        # numpy build tools (not needed at runtime)
        'numpy.distutils',
        'numpy.f2py',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='sidecar',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,         # UPX corrupts PyTorch DLLs
    console=True,      # keep console for stdout progress protocol
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='sidecar',
)
