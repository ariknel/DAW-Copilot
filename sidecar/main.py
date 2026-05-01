"""
AI MIDI Composer v2 sidecar - ACE-Step 1.5 audio generation
Stdout protocol: PROGRESS download/load <0-100>, READY, ERROR <msg>, INFO device=cuda|cpu
"""
import sys
if sys.platform == "win32":
    import asyncio
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

import argparse, io, os, re, signal, threading, time, traceback
from pathlib import Path
from typing import Optional

sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

_PIPELINE   = None
_DEVICE     = "cpu"
_READY      = threading.Event()
_LOAD_ERROR = ""
_GEN_LOCK   = threading.Lock()
_DIT_READY  = threading.Event()  # set when DiT is on GPU and ready
_DIT_READY.set()                 # starts True

def _emit(msg):       print(msg, flush=True)
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

        checkpoints_dir = str(model_dir / "checkpoints")
        os.environ.setdefault("ACESTEP_CHECKPOINTS_DIR", checkpoints_dir)
        os.environ.setdefault("ACESTEP_INIT_LLM", "false")
        os.environ.setdefault("ACESTEP_OFFLOAD_TO_CPU", "true")
        os.environ.setdefault("OMP_NUM_THREADS", "6")
        os.environ.setdefault("MKL_NUM_THREADS", "6")
        os.environ.setdefault("PYTORCH_CUDA_ALLOC_CONF", "expandable_segments:True")

        # Lower process priority so DAW stays responsive
        try:
            import psutil
            psutil.Process(os.getpid()).nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
            print("[LOAD] Process priority set to BELOW_NORMAL", flush=True)
        except Exception as e:
            print(f"[LOAD] Priority set failed: {e}", flush=True)

        # Configure SDPA backends
        torch.backends.cuda.enable_mem_efficient_sdp(True)
        torch.backends.cuda.enable_flash_sdp(False)
        torch.backends.cuda.enable_math_sdp(True)
        print("[LOAD] SDPA: mem_efficient=ON flash=OFF math=ON, shared memory UVM enabled", flush=True)

        # Patch eager->sdpa and 600s timeout in ACE-Step files
        for mod_name, find, replace in [
            ("acestep.core.generation.handler.init_service_loader", "eager", "sdpa"),
        ]:
            try:
                import importlib
                mod = importlib.import_module(mod_name)
                src_path = mod.__file__
                src = open(src_path, encoding="utf-8").read()
                if find in src:
                    open(src_path, "w", encoding="utf-8").write(src.replace(find, replace))
                    print(f"[LOAD] Patched {mod_name}: {find}->{replace}", flush=True)
                else:
                    print(f"[LOAD] {mod_name} already patched or no {find} found", flush=True)
            except PermissionError:
                print(f"[LOAD] Cannot patch {mod_name} - run as admin", flush=True)
            except Exception as e:
                print(f"[LOAD] Patch failed: {e}", flush=True)

        try:
            import acestep.core.generation.handler.generate_music_execute as _gme
            import re as _re
            src = open(_gme.__file__, encoding="utf-8").read()
            if "600" in src:
                open(_gme.__file__, "w", encoding="utf-8").write(
                    _re.sub(r'(?<!\d)600(?!\d)', '86400', src))
                print("[LOAD] Patched timeout 600s->86400s", flush=True)
        except PermissionError:
            print("[LOAD] Cannot patch timeout - run as admin", flush=True)
        except Exception as e:
            print(f"[LOAD] Timeout patch failed: {e}", flush=True)

        emit_download(10.0)

        # Install pytorch_wavelets if missing
        try:
            import pytorch_wavelets  # noqa
        except ImportError:
            print("[LOAD] Installing pytorch_wavelets...", flush=True)
            import subprocess
            subprocess.run([sys.executable, "-m", "pip", "install",
                            "pytorch_wavelets", "PyWavelets", "--quiet"], check=False)

        from acestep.handler import AceStepHandler
        emit_load(20.0)

        _PIPELINE = AceStepHandler()
        print(f"[LOAD] device={_DEVICE} initializing handler...", flush=True)

        _PIPELINE.initialize_service(
            project_root=str(model_dir),
            config_path="acestep-v15-turbo",
            device=_DEVICE,
        )

        # Patch VAE decode to offload DiT to CPU, freeing VRAM for GPU VAE
        # DiT reloads to GPU in background AFTER response is sent
        try:
            import acestep.core.generation.handler.generate_music_decode as _dec_mod
            _orig_decode = _dec_mod.GenerateMusicDecodeMixin._decode_generate_music_pred_latents

            def _patched_decode(self, *args, **kwargs):
                import torch as _torch
                # Find DiT model
                dit_attr = None
                for attr in ('model', 'dit_model', 'dit', 'transformer', 'unet'):
                    if hasattr(self, attr) and hasattr(getattr(self, attr), 'parameters'):
                        dit_attr = attr
                        break

                if dit_attr:
                    try:
                        _DIT_READY.clear()  # mark DiT as not ready
                        getattr(self, dit_attr).to('cpu')
                        _torch.cuda.empty_cache()
                        print(f"[VAE] Moved {dit_attr} to CPU for VAE decode", flush=True)
                    except Exception as e:
                        print(f"[VAE] Offload failed: {e}", flush=True)
                        dit_attr = None
                        _DIT_READY.set()

                result = _orig_decode(self, *args, **kwargs)

                # Keep DiT on CPU after VAE - it will reload when next generation starts
                # This keeps VRAM free between generations
                _DIT_READY.set()
                print(f"[VAE] {dit_attr} stays on CPU (VRAM freed for next gen)", flush=True)

                return result

            _dec_mod.GenerateMusicDecodeMixin._decode_generate_music_pred_latents = _patched_decode
            print("[LOAD] VAE decode patch applied: DiT will offload to CPU before VAE", flush=True)
        except Exception as e:
            print(f"[LOAD] VAE patch failed: {e}", flush=True)
            _DIT_READY.set()

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
    out = {"bars": None, "bpm": None, "key": None, "tempo_word": None}
    m = re.search(r'(\d+)\s*(?:bar|bars|measure)', p)
    if m: out["bars"] = int(m.group(1))
    m = re.search(r'(\d{2,3})\s*bpm', p)
    if m: out["bpm"] = int(m.group(1))
    m = re.search(r'\b([A-Ga-g][#b]?\s*(?:major|minor|maj|min)?)\b', prompt)
    if m: out["key"] = m.group(1).strip()
    if not out["bpm"]:
        for words, bpm, label in [
            (["very slow","largo"],                        50, "very slow"),
            (["slow","adagio","gentle","relaxed","chill"], 75, "slow"),
            (["medium","moderate","andante"],             105, "medium"),
            (["fast","upbeat","energetic","driving"],     135, "fast"),
            (["very fast","presto","intense"],            165, "very fast"),
        ]:
            if any(w in p for w in words):
                out["bpm"] = bpm; out["tempo_word"] = label; break
    return out


def _build_prompt(user_prompt, params):
    parts = []
    if params.get("bpm"):        parts.append(f"{params['bpm']} bpm")
    if params.get("key"):        parts.append(params["key"])
    if params.get("tempo_word"): parts.append(params["tempo_word"])
    parts.append(user_prompt.strip())
    # Always add looping/seamless hint for better loop output
    if not any(w in user_prompt.lower() for w in ["loop", "seamless", "repeat"]):
        parts.append("seamless loop")
    return ", ".join(parts)


def _calc_duration(params):
    bars = params.get("bars")
    bpm  = params.get("bpm") or 120
    return bars * 4 * (60.0 / bpm) if bars else 15.0


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
        import numpy as _np
        best = (None, -1.0)
        for s in range(12):
            for mode, prof in (("major", maj), ("minor", mnr)):
                score = float(_np.corrcoef(_np.roll(prof, s), chroma)[0,1])
                if score > best[1]: best = (f"{names[s]} {mode}", score)
        result["key"] = best[0] or ""
    except Exception:
        pass
    return result


def _generate(prompt, duration=None, guidance_scale=7.0):
    # Wait for any in-progress DiT operations to complete
    if not _DIT_READY.wait(timeout=120):
        raise RuntimeError("DiT model not ready (reload timed out)")

    # Clear CUDA cache to free fragmented memory before generation
    try:
        import torch
        torch.cuda.empty_cache()
        free_gb = torch.cuda.mem_get_info()[0] / 1e9
        print(f"[GEN] VRAM free before generation: {free_gb:.2f}GB", flush=True)
    except Exception:
        pass

    # Reload DiT to GPU if it was offloaded after previous VAE decode
    try:
        import torch
        if _PIPELINE is not None:
            for attr in ('model', 'dit_model', 'dit', 'transformer', 'unet'):
                if hasattr(_PIPELINE, attr):
                    obj = getattr(_PIPELINE, attr)
                    if hasattr(obj, 'parameters'):
                        # Check if already on GPU
                        try:
                            p = next(obj.parameters())
                            if p.device.type == 'cpu':
                                print(f"[GEN] Reloading {attr} to GPU for generation...", flush=True)
                                obj.to(_DEVICE)
                                torch.cuda.empty_cache()
                                print(f"[GEN] {attr} reloaded to GPU", flush=True)
                        except StopIteration:
                            pass
                        break
    except Exception as e:
        print(f"[GEN] DiT reload check failed: {e}", flush=True)

    parsed   = _parse_params(prompt)
    caption  = _build_prompt(prompt, parsed)
    bpm      = parsed.get("bpm") or 120
    bars     = parsed.get("bars")

    if duration is None:
        duration = _calc_duration(parsed)
    duration = max(8.0, min(120.0, duration))

    print(f"[GEN] caption={caption[:100]} duration={duration:.1f}s bpm={bpm}", flush=True)

    import tempfile
    from acestep.inference import GenerationParams, GenerationConfig, generate_music

    gen_params = GenerationParams(
        caption=caption,
        lyrics="[inst]",
        duration=duration,
        guidance_scale=guidance_scale,
        inference_steps=8,
    )
    gen_config = GenerationConfig(batch_size=1, audio_format="wav")

    with tempfile.TemporaryDirectory() as tmp_dir:
        result = generate_music(_PIPELINE, None, gen_params, gen_config, save_dir=tmp_dir)
        print(f"[GEN] result type={type(result)}", flush=True)

        if isinstance(result, list):
            result = result[0] if result else None
        if result is None:
            raise RuntimeError("generate_music returned no results")

        wav_bytes = None
        if hasattr(result, "success") and hasattr(result, "audios"):
            if not result.success:
                raise RuntimeError(f"Generation failed: {getattr(result, 'error', result.status_message)}")
            if result.audios:
                audio_info = result.audios[0]
                audio_path = audio_info.get("path") if isinstance(audio_info, dict) else getattr(audio_info, "path", None)
                if audio_path and os.path.exists(str(audio_path)):
                    wav_bytes = open(str(audio_path), "rb").read()

        if wav_bytes is None:
            for attr in ("audio_path", "path", "file"):
                p = getattr(result, attr, None)
                if p and os.path.exists(str(p)):
                    wav_bytes = open(str(p), "rb").read()
                    break

        if wav_bytes is None:
            wavs = list(Path(tmp_dir).glob("*.wav"))
            if wavs: wav_bytes = wavs[0].read_bytes()

        if not wav_bytes:
            raise RuntimeError(f"Cannot extract audio from result: {dir(result)}")

    print(f"[GEN] wav={len(wav_bytes)} bytes", flush=True)

    # Run analysis with timeout - don't block response if librosa hangs
    midi_bytes = None
    meta = {"key": "", "tempo": ""}
    pool = __import__('concurrent.futures', fromlist=['ThreadPoolExecutor']).ThreadPoolExecutor(max_workers=1)
    meta_future = pool.submit(_analyse_wav, wav_bytes)
    try:
        meta = meta_future.result(timeout=8)
    except Exception as e:
        print(f"[GEN] Analysis skipped: {e}", flush=True)
    pool.shutdown(wait=False)

    bars_str = f"{bars} bars" if bars else ""
    parts = [p for p in [meta.get("key"), meta.get("tempo"), bars_str] if p]
    summary = ("Generated: " + ", ".join(parts) + ".") if parts else "Generated. Drag into your DAW."

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
                "device": _DEVICE, "model": "ace-step-v1.5-turbo",
                "dit_ready": _DIT_READY.is_set()}

    @app.post("/generate")
    def generate(req: GenReq):
        if not _READY.is_set():
            return {"success": False, "error": "Model not ready."}
        with _GEN_LOCK:
            t0 = time.time()
            try:
                r = _generate(req.prompt, req.duration, req.guidance_scale)
            except BaseException as e:
                tb = traceback.format_exc()
                print(f"[GEN] EXCEPTION in _generate: {type(e).__name__}: {e}", flush=True)
                print(tb, flush=True)
                return {"success": False, "error": f"{type(e).__name__}: {e}"}

            print(f"[GEN] _generate returned, wav_bytes={len(r.get('wav_bytes') or b'')}", flush=True)

            out_dir = Path(os.environ.get("LOCALAPPDATA", os.environ.get("TEMP", "C:\\Temp"))) \
                      / "AIMidiComposer" / "output"
            try:
                out_dir.mkdir(parents=True, exist_ok=True)
            except Exception as e:
                print(f"[GEN] Cannot create output dir: {e}", flush=True)
                out_dir = Path(os.environ.get("TEMP", "C:\\Temp"))

            wav_path = ""
            midi_path = ""

            if r["wav_bytes"]:
                try:
                    wf = out_dir / f"gen_{int(t0)}.wav"
                    wf.write_bytes(r["wav_bytes"])
                    wav_path = str(wf)
                    print(f"[GEN] saved wav -> {wav_path} ({len(r['wav_bytes'])} bytes)", flush=True)
                except Exception as e:
                    print(f"[GEN] ERROR saving wav: {e}", flush=True)

            print(f"[GEN] returning response: wav_path={wav_path}", flush=True)
            return {
                "success":   True,
                "wav_path":  wav_path,
                "midi_path": midi_path,
                "key":       r["key"],
                "tempo":     r["tempo"],
                "summary":   r["summary"],
                "seconds":   round(time.time() - t0, 1),
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
