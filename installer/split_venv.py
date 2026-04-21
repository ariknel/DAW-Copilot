"""
Zip sidecar/.venv into a single venv.zip for Inno Setup 6.
Run from project root: python installer/split_venv.py
"""
import zipfile, pathlib, sys

venv    = pathlib.Path("sidecar/.venv").resolve()
out_dir = pathlib.Path("sidecar/dist")
out     = out_dir / "venv.zip"

if not venv.exists():
    print(f"[ERROR] venv not found at {venv}")
    sys.exit(1)

out_dir.mkdir(parents=True, exist_ok=True)

# Remove old split parts if they exist
for old in out_dir.glob("venv_*.zip"):
    old.unlink()
    print(f"  Removed {old.name}")

if out.exists():
    out.unlink()

files = sorted(f for f in venv.rglob("*") if f.is_file())
print(f"  Files: {len(files)}")
print(f"  Output: {out}")

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    for i, f in enumerate(files):
        if i % 2000 == 0:
            print(f"  {i}/{len(files)}...", flush=True)
        z.write(f, f.relative_to(venv))

print(f"  venv.zip: {out.stat().st_size:,} bytes")
print("  Done.")
