"""
AI MIDI Composer - Python sidecar.

Runs as a child process of the JUCE VST. Communicates via:
  - stdout: newline-delimited progress protocol (PROGRESS download 37.2, READY, ERROR ...)
  - HTTP:   POST /generate, GET /healthz, POST /shutdown on 127.0.0.1:<port>

Model: slseanwu/MIDI-LLM_Llama-3.2-1B (NeurIPS AI4Music 2025)
"""

# NOTE: do NOT add "from __future__ import annotations" here.
# It makes ALL annotations lazy strings, which breaks pydantic model
# resolution when models are defined inside functions (like GenReq in
# build_app). pydantic sees "GenReq" as a forward ref and can't find it.

import argparse
import base64
import io
import json
import os
import re
import signal
import sys
import threading
import time
import traceback
from pathlib import Path
from typing import Optional

# Unbuffered stdout so the host reads progress immediately
sys.stdout.reconfigure(line_buffering=True)  # type: ignore

# -- Pre-import numpy and torch on the MAIN thread before anything else -------
# PyInstaller's module loader is NOT thread-safe. If torch/numpy are first
# imported from a background thread while FastAPI/pydantic also triggers
# imports on the main thread, you get:
#   ImportError: cannot load module more than once per process
# Solution: force both onto the main thread right here, before any threads start.
try:
    import numpy as _np          # noqa - side effect: registers in sys.modules
    import torch as _torch       # noqa - side effect: registers in sys.modules
    del _np, _torch              # don't pollute module namespace
except Exception as _e:
    print(f"ERROR Pre-import failed: {_e}", flush=True)
    traceback.print_exc()
    sys.exit(1)

# -- Progress protocol helpers ------------------------------------------------
def _emit(line: str) -> None:
    print(line, flush=True)

def emit_download(pct: float) -> None: _emit(f"PROGRESS download {pct:.1f}")
def emit_load(pct: float)     -> None: _emit(f"PROGRESS load {pct:.1f}")
def emit_ready()              -> None: _emit("READY")
def emit_error(msg: str)      -> None: _emit(f"ERROR {msg}")


# -- Globals (populated by loader thread) -------------------------------------
_MODEL = None          # transformers model
_TOKENIZER = None      # text tokenizer
_MIDI_TOKENIZER = None # AMT tokenizer (anticipation library)
_DEVICE = "cpu"
_READY = threading.Event()
_LOAD_ERROR: Optional[str] = None
_GEN_LOCK = threading.Lock()   # only one inference at a time
_MODEL_ID = "slseanwu/MIDI-LLM_Llama-3.2-1B"


def _load_everything(model_dir: Path) -> None:
    """Download (if needed) + load model into memory. Runs in background thread."""
    global _MODEL, _TOKENIZER, _MIDI_TOKENIZER, _DEVICE, _LOAD_ERROR
    try:
        emit_download(0.0)

        # torch and numpy are already imported at module level (main thread).
        # Just reference them here.
        import torch
        from huggingface_hub import snapshot_download

        # -- 1. Select device -------------------------------------------------
        if torch.cuda.is_available():
            _DEVICE = "cuda"
        else:
            _DEVICE = "cpu"
        _emit(f"INFO device={_DEVICE}")

        # -- 2. Download weights with per-file progress via tqdm callback -----
        # snapshot_download respects HF_HOME; we set it explicitly to keep
        # the cache inside the user-chosen model directory.
        os.environ["HF_HOME"] = str(model_dir)
        os.environ["HF_HUB_CACHE"] = str(model_dir / "hub")
        (model_dir / "hub").mkdir(parents=True, exist_ok=True)

        # Poll download size for progress emissions (HF progress bars don't
        # play nicely with our protocol)
        poll_stop = threading.Event()
        expected_bytes = 3_100_000_000  # MIDI-LLM 1B bf16 is ~3GB

        def _poll():
            while not poll_stop.is_set():
                total = 0
                for root, _dirs, files in os.walk(model_dir / "hub"):
                    for f in files:
                        try:
                            total += os.path.getsize(os.path.join(root, f))
                        except OSError:
                            pass
                pct = min(99.0, 100.0 * total / expected_bytes)
                emit_download(pct)
                poll_stop.wait(2.0)

        poll_thread = threading.Thread(target=_poll, daemon=True)
        poll_thread.start()
        try:
            snapshot_download(
                repo_id=_MODEL_ID,
                cache_dir=str(model_dir / "hub"),
                local_files_only=False,
            )
        finally:
            poll_stop.set()
            poll_thread.join(timeout=1)

        emit_download(100.0)

        # -- 3. Load tokenizer + model ----------------------------------------
        emit_load(5.0)
        from transformers import AutoTokenizer, AutoModelForCausalLM
        import torch

        _TOKENIZER = AutoTokenizer.from_pretrained(_MODEL_ID, cache_dir=str(model_dir / "hub"))
        emit_load(25.0)

        dtype = torch.bfloat16 if _DEVICE == "cuda" else torch.float32
        _MODEL = AutoModelForCausalLM.from_pretrained(
            _MODEL_ID,
            cache_dir=str(model_dir / "hub"),
            torch_dtype=dtype,
            low_cpu_mem_usage=True,
        ).to(_DEVICE)
        _MODEL.eval()
        emit_load(85.0)

        # -- 4. Prepare the AMT MIDI detokenizer ------------------------------
        # MIDI-LLM uses the vocabulary from Anticipatory Music Transformer.
        # The 'anticipation' package provides events_to_midi.
        try:
            from anticipation.convert import events_to_midi  # noqa
            _MIDI_TOKENIZER = events_to_midi
        except ImportError:
            emit_error("anticipation package not installed - cannot detokenize MIDI")
            _LOAD_ERROR = "anticipation missing"
            return

        emit_load(100.0)
        _READY.set()
        emit_ready()

    except Exception as e:  # pragma: no cover
        _LOAD_ERROR = f"{type(e).__name__}: {e}"
        emit_error(_LOAD_ERROR)
        traceback.print_exc(file=sys.stderr)


# -- MIDI-LLM generation ------------------------------------------------------
def _generate_midi(prompt: str, temperature: float, top_p: float, max_tokens: int) -> bytes:
    """Run inference and return a combined multitrack MIDI file as bytes."""
    import torch

    # MIDI-LLM expects a fixed system prompt (per model card)
    full_prompt = (
        "You are a world-class composer. Please compose some music "
        f"according to the following description: {prompt}"
    )

    input_ids = _TOKENIZER.encode(full_prompt, return_tensors="pt").to(_DEVICE)

    # The MIDI vocabulary starts at id >= 128256 (text vocab size).
    # Generation continues until EOS or max_tokens reached.
    with torch.no_grad():
        out_ids = _MODEL.generate(
            input_ids,
            max_new_tokens=max_tokens,
            do_sample=True,
            temperature=temperature,
            top_p=top_p,
            pad_token_id=_TOKENIZER.eos_token_id,
        )

    # Strip the prompt tokens - keep only newly generated ones
    new_ids = out_ids[0, input_ids.shape[1]:].tolist()

    # Convert model token IDs -> AMT event tuples -> MIDI
    # AMT MIDI tokens occupy ids >= text_vocab_size (128256 in Llama 3.2)
    text_vocab_size = 128_256
    midi_tokens = [t - text_vocab_size for t in new_ids if t >= text_vocab_size]

    if not midi_tokens:
        raise RuntimeError("Model produced no MIDI tokens.")

    midi_obj = _MIDI_TOKENIZER(midi_tokens)   # -> mido.MidiFile

    buf = io.BytesIO()
    midi_obj.save(file=buf)
    return buf.getvalue()


# -- Post-processing: detect key / tempo / time-signature from MIDI bytes -----
def _analyse_midi(midi_bytes: bytes) -> dict:
    import mido
    mid = mido.MidiFile(file=io.BytesIO(midi_bytes))

    tempo_bpm = None
    timesig = None
    for track in mid.tracks:
        for msg in track:
            if msg.type == "set_tempo" and tempo_bpm is None:
                tempo_bpm = round(mido.tempo2bpm(msg.tempo))
            elif msg.type == "time_signature" and timesig is None:
                timesig = f"{msg.numerator}/{msg.denominator}"
        if tempo_bpm is not None and timesig is not None:
            break

    # Very rough key estimate from note histogram vs major/minor profiles
    # (good enough for chat metadata; users don't expect music-theoretic precision)
    try:
        import numpy as np
        notes = []
        for track in mid.tracks:
            for msg in track:
                if msg.type == "note_on" and msg.velocity > 0:
                    notes.append(msg.note % 12)
        key_str = None
        if notes:
            hist = np.bincount(notes, minlength=12).astype(float)
            hist /= hist.sum() + 1e-9
            names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
            # Krumhansl-Kessler profiles
            maj = np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88])
            mnr = np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
            best = (None, -1.0)
            for shift in range(12):
                for mode, prof in (("major", maj), ("minor", mnr)):
                    score = float(np.corrcoef(np.roll(prof, shift), hist)[0, 1])
                    if score > best[1]:
                        best = (f"{names[shift]} {mode}", score)
            key_str = best[0]
    except Exception:
        key_str = None

    return {
        "key": key_str or "",
        "tempo": (f"{tempo_bpm} BPM" if tempo_bpm else ""),
        "time_signature": timesig or "",
    }


# -- HTTP layer ---------------------------------------------------------------
def build_app():
    from fastapi import FastAPI
    from pydantic import BaseModel

    app = FastAPI()

    class GenReq(BaseModel):
        prompt: str
        temperature: float = 0.9
        top_p: float = 0.98
        max_tokens: int = 2000

    @app.get("/healthz")
    def healthz():
        return {"ready": _READY.is_set(), "error": _LOAD_ERROR}

    @app.post("/generate")
    def generate(req: GenReq):
        if not _READY.is_set():
            return {"success": False, "error": "Model not ready."}
        with _GEN_LOCK:
            t0 = time.time()
            try:
                midi_bytes = _generate_midi(req.prompt, req.temperature, req.top_p, req.max_tokens)
            except Exception as e:
                traceback.print_exc(file=sys.stderr)
                return {"success": False, "error": f"{type(e).__name__}: {e}"}
            meta = _analyse_midi(midi_bytes)

            return {
                "success": True,
                "midi_base64": base64.b64encode(midi_bytes).decode("ascii"),
                "key": meta["key"],
                "tempo": meta["tempo"],
                "time_signature": meta["time_signature"],
                "summary": _compose_summary(req.prompt, meta),
                "seconds": time.time() - t0,
            }

    @app.post("/shutdown")
    def shutdown():
        os.kill(os.getpid(), signal.SIGTERM)
        return {"ok": True}

    return app


def _compose_summary(prompt: str, meta: dict) -> str:
    bits = []
    if meta["key"]:            bits.append(f"key of {meta['key']}")
    if meta["tempo"]:          bits.append(meta["tempo"])
    if meta["time_signature"]: bits.append(f"{meta['time_signature']} time")
    if not bits:
        return "Here's a composition matching your prompt. Drag any stem into your DAW."
    return "Composed in " + ", ".join(bits) + ". Drag any stem into your DAW."


# -- Entry point --------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--model-dir", type=Path, required=True)
    ap.add_argument("--parent-pid", type=int, default=0,
                    help="If set, sidecar self-terminates when parent dies.")
    args = ap.parse_args()

    args.model_dir.mkdir(parents=True, exist_ok=True)

    # Watchdog: kill self if parent PID dies (backup to Windows Job Object)
    if args.parent_pid:
        def _watchdog():
            import psutil
            while True:
                if not psutil.pid_exists(args.parent_pid):
                    os._exit(0)
                time.sleep(1.0)
        threading.Thread(target=_watchdog, daemon=True).start()

    # Kick off background model load
    threading.Thread(
        target=_load_everything, args=(args.model_dir,), daemon=True
    ).start()

    # Serve HTTP immediately so /healthz works during loading
    import uvicorn
    app = build_app()
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="warning")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
