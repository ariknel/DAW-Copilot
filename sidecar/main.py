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

# CRITICAL: Force the Selector event loop on Windows BEFORE any asyncio import.
# The default ProactorEventLoop (IOCP) crashes with WinError 64 during model
# loading when the socket accept loop hits a transient network condition.
# SelectorEventLoop is stable and fully compatible with uvicorn + FastAPI.
if sys.platform == "win32":
    import asyncio
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

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
        try:
            _MODEL = AutoModelForCausalLM.from_pretrained(
                _MODEL_ID,
                cache_dir=str(model_dir / "hub"),
                torch_dtype=dtype,
                low_cpu_mem_usage=True,
            )
            emit_load(60.0)
            if _DEVICE == "cuda":
                free_vram = torch.cuda.mem_get_info()[0]
                model_size = sum(p.numel() * p.element_size() for p in _MODEL.parameters())
                print(f"[DEBUG] VRAM free={round(free_vram/1e9,2)}GB model_size={round(model_size/1e9,2)}GB", flush=True)
                if free_vram < model_size * 1.1:
                    emit_error(
                        "Not enough VRAM to load model.\n"
                        "Need ~" + str(round(model_size / 1e9, 1)) + " GB free, have "
                        + str(round(free_vram / 1e9, 1)) + " GB.\n"
                        "Close other GPU applications and retry."
                    )
                    return
                _MODEL = _MODEL.to(_DEVICE)
        except (AssertionError, RuntimeError) as e:
            import traceback as _tb
            full_tb = _tb.format_exc()
            print(full_tb, flush=True)
            emit_error("Model load failed:\n" + full_tb[-600:])
            return
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

    input_ids = _TOKENIZER.encode(full_prompt, return_tensors="pt")

    # Ensure model and inputs are on the same device
    actual_device = next(_MODEL.parameters()).device
    print(f"[DEBUG] _DEVICE={_DEVICE} model_device={actual_device}", flush=True)
    input_ids = input_ids.to(actual_device)

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

    # The model embedding matrix splits at 128256 (per model card: "extends Llama 3.2's
    # vocabulary of 128,256 tokens"). tokenizer.vocab_size returns 128000 which is the
    # Llama 3.2 *tokenizer* vocab, but the embedding uses 128256 as the boundary.
    MIDI_OFFSET = 128_256
    all_midi = [t - MIDI_OFFSET for t in new_ids if t >= MIDI_OFFSET]

    if not all_midi:
        MIDI_OFFSET = 128_000
        all_midi = [t - MIDI_OFFSET for t in new_ids if t >= MIDI_OFFSET]

    print(f"[DEBUG] offset={MIDI_OFFSET} generated={len(new_ids)} "
          f"midi_count={len(all_midi)} "
          f"min={min(all_midi) if all_midi else 'N/A'} "
          f"max={max(all_midi) if all_midi else 'N/A'}", flush=True)

    if not all_midi:
        raise RuntimeError(f"Model produced no MIDI tokens. Sample: {new_ids[:10]}")

    # The anticipation library vocab layout (from vocab.py):
    #   TIME_OFFSET=0,     valid time tokens:  0 <= t < 10000
    #   DUR_OFFSET=10000,  valid dur tokens:   10000 <= t < 11000
    #   NOTE_OFFSET=11000, valid note tokens:  11000 <= t < 27513
    #   CONTROL_OFFSET=27513 (anticipated tokens, interleaved for infilling)
    #
    # The model generates triplets: (time, duration, note) interleaved with
    # anticipated control tokens. We extract only complete valid triplets.
    # Build triplets by scanning for valid (time, dur, note) sequences.
    TIME_MIN, TIME_MAX = 0, 9999
    DUR_MIN,  DUR_MAX  = 10000, 10999
    NOTE_MIN, NOTE_MAX = 11000, 27512

    triplets = []
    i = 0
    while i < len(all_midi) - 2:
        t0, t1, t2 = all_midi[i], all_midi[i+1], all_midi[i+2]
        if (TIME_MIN <= t0 <= TIME_MAX and
            DUR_MIN  <= t1 <= DUR_MAX  and
            NOTE_MIN <= t2 <= NOTE_MAX):
            triplets.extend([t0, t1, t2])
            i += 3
        else:
            i += 1  # skip and rescan

    print(f"[DEBUG] valid triplets extracted: {len(triplets)//3} notes "
          f"({len(triplets)} tokens)", flush=True)

    if not triplets:
        raise RuntimeError(
            f"No valid (time, duration, note) triplets found in {len(all_midi)} tokens. "
            f"Sample: {all_midi[:15]}"
        )

    midi_obj = _MIDI_TOKENIZER(triplets)

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
                import traceback as _tb
                tb = _tb.format_exc()
                print(tb, flush=True)
                return {"success": False, "error": f"{type(e).__name__}: {e}\n\n{tb[-400:]}"}
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
    # Use asyncio loop explicitly - avoids WinError 64 IOCP proactor crash on Windows
    uvicorn.run(app, host="127.0.0.1", port=args.port, log_level="warning", loop="asyncio")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
