"""
AI MIDI Composer - Python sidecar (v2: ACE-Step 1.5 + Basic Pitch)

Stdout protocol:
  PROGRESS download <0-100>
  PROGRESS load <0-100>
  READY
  ERROR <message>
  INFO device=cuda|cpu

HTTP API (127.0.0.1:<port>):
  GET  /healthz        -> {ready, device, model}
  POST /generate       -> {success, wav_base64, midi_base64, summary, key, tempo}
  POST /shutdown
"""

import sys
if sys.platform == "win32":
    import asyncio
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

import argparse
import base64
import io
import os
import re
import signal
import threading
import time
import traceback
from pathlib import Path
from typing import Optional

sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

_PIPELINE   = None
_DEVICE     = "cpu"
_READY      = threading.Event()
_LOAD_ERROR = ""
_GEN_LOCK   = threading.Lock()


def _emit(msg):   print(msg, flush=True)
def emit_download(p): _emit(f"PROGRESS download {p:.1f}")
def emit_load(p):     _emit(f"PROGRESS load {p:.1f}")
def emit_ready():     _emit("READY")
def emit_error(m):    _emit(f"ERROR {m}")


def _load_everything(model_dir):
    global _PIPELINE, _DEVICE, _LOAD_ERROR
    try:
        import torch

        emit_download(0.0)
        _DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
        _emit(f"INFO device={_DEVICE}")
        emit_load(5.0)

        # Set HF cache to user model dir so weights persist across reinstalls
        hf_cache = str(model_dir / "hf_cache")
        os.environ["HF_HOME"] = hf_cache
        os.environ["HUGGINGFACE_HUB_CACHE"] = hf_cache
        Path(hf_cache).mkdir(parents=True, exist_ok=True)

        emit_download(10.0)

        # Import ACE-Step pipeline
        try:
            from ace_step.pipeline import ACEStepPipeline
        except ImportError:
            from acestep.pipeline import ACEStepPipeline

        emit_load(20.0)

        # Load - first run downloads ~4GB weights automatically
        _PIPELINE = ACEStepPipeline.from_pretrained(
            model_name="ace-step-v1.5-base",
            device=_DEVICE,
        )

        emit_download(100.0)
        emit_load(100.0)
        _READY.set()
        emit_ready()

    except Exception as e:
        _LOAD_ERROR = f"{type(e).__name__}: {e}"
        print(traceback.format_exc(), flush=True)
        emit_error(_LOAD_ERROR)


def _parse_params(prompt):
    p = prompt.lower()
    params = {"bars": None, "bpm": None, "key": None, "tempo_word": None}

    m = re.search(r'(\d+)\s*(?:bar|bars|measure|measures)', p)
    if m: params["bars"] = int(m.group(1))

    m = re.search(r'(\d{2,3})\s*bpm', p)
    if m: params["bpm"] = int(m.group(1))

    m = re.search(r'\b([A-Ga-g][#b]?\s*(?:major|minor|maj|min)?)\b', prompt)
    if m: params["key"] = m.group(1).strip()

    if not params["bpm"]:
        for words, bpm, label in [
            (["very slow","largo"],                        50, "very slow"),
            (["slow","adagio","gentle","relaxed","chill"], 75, "slow"),
            (["medium","moderate","andante"],             105, "medium"),
            (["fast","upbeat","energetic","driving"],     135, "fast"),
            (["very fast","presto","intense"],            165, "very fast"),
        ]:
            if any(w in p for w in words):
                params["bpm"] = bpm
                params["tempo_word"] = label
                break

    return params


def _build_prompt(user_prompt, params):
    parts = []
    if params.get("bpm"):        parts.append(f"{params['bpm']} bpm")
    if params.get("key"):        parts.append(params["key"])
    if params.get("tempo_word"): parts.append(params["tempo_word"])
    parts.append(user_prompt.strip())
    return ", ".join(parts)


def _calc_duration(params):
    bars = params.get("bars")
    bpm  = params.get("bpm") or 120
    if bars:
        return bars * 4 * (60.0 / bpm)
    return 30.0


def _audio_to_midi(wav_bytes, bpm=120):
    """
    Transcribe WAV to MIDI using librosa pitch tracking + pretty_midi.
    No TensorFlow dependency. Works best on single-instrument audio.
    """
    try:
        import librosa
        import pretty_midi
        import numpy as np
        import soundfile as sf
        import io as _io

        # Load audio
        audio, sr = sf.read(_io.BytesIO(wav_bytes))
        if audio.ndim == 2:
            audio = audio.mean(axis=1)
        audio = audio.astype(np.float32)

        # Pitch tracking via pYIN (librosa) - good for melodic content
        f0, voiced_flag, voiced_probs = librosa.pyin(
            audio, sr=sr,
            fmin=librosa.note_to_hz("C2"),
            fmax=librosa.note_to_hz("C7"),
            frame_length=2048,
        )

        # Convert f0 to MIDI notes
        times = librosa.times_like(f0, sr=sr, hop_length=512)
        midi = pretty_midi.PrettyMIDI(initial_tempo=float(bpm))
        instrument = pretty_midi.Instrument(program=0)  # Acoustic Grand Piano

        # Merge consecutive voiced frames into notes
        note_start = None
        note_pitch = None
        for i, (t, freq, voiced) in enumerate(zip(times, f0, voiced_flag)):
            if voiced and freq is not None and not np.isnan(freq):
                pitch = int(round(librosa.hz_to_midi(freq)))
                pitch = max(0, min(127, pitch))
                if note_pitch is None:
                    note_start = t
                    note_pitch = pitch
                elif abs(pitch - note_pitch) > 1:
                    # Pitch changed - end previous note
                    if note_start is not None and t - note_start > 0.05:
                        note = pretty_midi.Note(
                            velocity=80, pitch=note_pitch,
                            start=note_start, end=t
                        )
                        instrument.notes.append(note)
                    note_start = t
                    note_pitch = pitch
            else:
                if note_start is not None and note_pitch is not None:
                    end_t = t
                    if end_t - note_start > 0.05:
                        note = pretty_midi.Note(
                            velocity=80, pitch=note_pitch,
                            start=note_start, end=end_t
                        )
                        instrument.notes.append(note)
                note_start = None
                note_pitch = None

        if not instrument.notes:
            print("[WARN] audio_to_midi: no notes detected via pYIN", flush=True)
            return None

        midi.instruments.append(instrument)
        buf = _io.BytesIO()
        midi.write(buf)
        print(f"[GEN] MIDI: {len(instrument.notes)} notes transcribed", flush=True)
        return buf.getvalue()

    except ImportError as e:
        print(f"[WARN] audio_to_midi missing dep: {e}", flush=True)
        return None
    except Exception as e:
        print(f"[WARN] audio_to_midi failed: {e}", flush=True)
        return None


def _analyse_wav(wav_bytes):
    result = {"key": "", "tempo": ""}
    try:
        import librosa, numpy as np, soundfile as sf
        audio, sr = sf.read(io.BytesIO(wav_bytes))
        if audio.ndim == 2: audio = audio.mean(axis=1)
        audio = audio.astype(float)
        tempo, _ = librosa.beat.beat_track(y=audio, sr=sr)
        if tempo: result["tempo"] = f"{int(round(float(tempo)))} BPM"
        chroma = librosa.feature.chroma_cqt(y=audio, sr=sr).mean(axis=1)
        names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
        maj = [6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88]
        mnr = [6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17]
        import numpy as np
        maj, mnr = np.array(maj), np.array(mnr)
        best = (None, -1.0)
        for s in range(12):
            for mode, prof in (("major", maj), ("minor", mnr)):
                score = float(np.corrcoef(np.roll(prof, s), chroma)[0,1])
                if score > best[1]: best = (f"{names[s]} {mode}", score)
        result["key"] = best[0] or ""
    except Exception:
        pass
    return result


def _generate(prompt, duration=None, guidance_scale=7.0):
    params = _parse_params(prompt)
    audio_prompt = _build_prompt(prompt, params)
    bpm = params.get("bpm") or 120

    if duration is None:
        duration = _calc_duration(params)
    duration = max(10.0, min(600.0, duration))

    print(f"[GEN] prompt={audio_prompt[:120]} duration={duration:.1f}s", flush=True)

    result = _PIPELINE.generate(
        prompt=audio_prompt,
        duration=duration,
        guidance_scale=guidance_scale,
        num_inference_steps=60,
    )

    # Normalise pipeline output to wav_bytes
    if isinstance(result, bytes):
        wav_bytes = result
    elif hasattr(result, "audio"):
        import soundfile as sf
        buf = io.BytesIO()
        audio = result.audio
        sr = getattr(result, "sample_rate", 44100)
        sf.write(buf, audio, sr, format="WAV")
        wav_bytes = buf.getvalue()
    elif isinstance(result, (str, Path)):
        wav_bytes = Path(result).read_bytes()
    else:
        raise RuntimeError(f"Unknown pipeline output: {type(result)}")

    print(f"[GEN] wav={len(wav_bytes)} bytes", flush=True)

    midi_bytes = _audio_to_midi(wav_bytes, bpm=bpm)
    meta       = _analyse_wav(wav_bytes)

    parts = []
    if meta["key"]:    parts.append(f"key of {meta['key']}")
    if meta["tempo"]:  parts.append(meta["tempo"])
    if params["bars"]: parts.append(f"{params['bars']} bars")
    summary = ("Generated: " + ", ".join(parts) + ".") if parts else \
              "Generated. Drag stems into your DAW."

    return {"wav_bytes": wav_bytes, "midi_bytes": midi_bytes,
            "key": meta["key"], "tempo": meta["tempo"], "summary": summary}


def build_app():
    from fastapi import FastAPI
    from pydantic import BaseModel

    app = FastAPI()

    class GenReq(BaseModel):
        prompt:         str
        duration:       Optional[float] = None
        guidance_scale: float = 7.0

    @app.get("/healthz")
    def healthz():
        return {"ready": _READY.is_set(), "error": _LOAD_ERROR,
                "device": _DEVICE, "model": "ace-step-v1.5"}

    @app.post("/generate")
    def generate(req: GenReq):
        if not _READY.is_set():
            return {"success": False, "error": "Model not ready."}
        with _GEN_LOCK:
            t0 = time.time()
            try:
                r = _generate(req.prompt, req.duration, req.guidance_scale)
            except Exception as e:
                tb = traceback.format_exc()
                print(tb, flush=True)
                return {"success": False, "error": f"{type(e).__name__}: {e}"}

            return {
                "success":      True,
                "wav_base64":   base64.b64encode(r["wav_bytes"]).decode(),
                "midi_base64":  base64.b64encode(r["midi_bytes"]).decode() if r["midi_bytes"] else "",
                "key":          r["key"],
                "tempo":        r["tempo"],
                "summary":      r["summary"],
                "seconds":      round(time.time() - t0, 1),
            }

    @app.post("/shutdown")
    def shutdown():
        os.kill(os.getpid(), signal.SIGTERM)
        return {"ok": True}

    return app


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port",       type=int,  required=True)
    ap.add_argument("--model-dir",  type=Path, required=True)
    ap.add_argument("--parent-pid", type=int,  default=0)
    args = ap.parse_args()
    args.model_dir.mkdir(parents=True, exist_ok=True)

    if args.parent_pid:
        def _watchdog():
            try:
                import psutil
                while True:
                    if not psutil.pid_exists(args.parent_pid): os._exit(0)
                    time.sleep(1.0)
            except ImportError: pass
        threading.Thread(target=_watchdog, daemon=True).start()

    threading.Thread(target=_load_everything, args=(args.model_dir,), daemon=True).start()

    import uvicorn
    uvicorn.run(build_app(), host="127.0.0.1", port=args.port,
                log_level="warning", loop="asyncio")


if __name__ == "__main__":
    main()
