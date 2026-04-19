# rthook_syspath.py
# PyInstaller runtime hook - runs before main.py
#
# Fixes: ImportError: cannot load module more than once per process
# This happens when PyInstaller's bootloader adds the same directory
# to sys.path multiple times (e.g. via .pth files), causing Python to
# find and attempt to load the same C extension (.pyd) via two paths.
# torch and tokenizers are especially prone to this.
import sys
import os

# Deduplicate sys.path, preserving order
seen = set()
new_path = []
for p in sys.path:
    # Normalize the path so c:\foo and C:\Foo are the same
    norm = os.path.normcase(os.path.normpath(p))
    if norm not in seen:
        seen.add(norm)
        new_path.append(p)
sys.path[:] = new_path

# Also prevent the double-import of C extensions that torch triggers.
# Setting this env var tells PyInstaller-frozen torch not to re-add
# its lib dir if it is already present.
os.environ.setdefault("TORCH_DISABLE_RPATH_INJECTION", "1")
