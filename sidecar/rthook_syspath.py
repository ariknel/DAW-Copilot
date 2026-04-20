# rthook_syspath.py - runs before main.py in frozen exe
import sys, os

# Deduplicate sys.path
seen, new_path = set(), []
for p in sys.path:
    norm = os.path.normcase(os.path.normpath(p))
    if norm not in seen:
        seen.add(norm); new_path.append(p)
sys.path[:] = new_path

os.environ.setdefault("TORCH_DISABLE_RPATH_INJECTION", "1")

# Diagnostic: print what ace_step/acestep packages are visible
print("[rthook] sys.path entries:", len(sys.path), flush=True)
for name in ("ace_step", "acestep", "acestep.pipeline", "ace_step.pipeline"):
    try:
        import importlib
        spec = importlib.util.find_spec(name)
        print(f"[rthook] {name}: {spec.origin if spec else 'NOT FOUND'}", flush=True)
    except Exception as e:
        print(f"[rthook] {name}: ERROR {e}", flush=True)
