# 🎹 AI DAW Copilot

> **A VST3 plugin that turns natural-language prompts into multitrack audio stems and MIDI — powered by ACE-Step audio generation and a dedicated audio-to-MIDI conversion engine.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://www.microsoft.com/windows)
[![VST3](https://img.shields.io/badge/Plugin%20Format-VST3-orange.svg)](https://www.steinberg.net/vst3/)
[![JUCE 8](https://img.shields.io/badge/Framework-JUCE%208-green.svg)](https://juce.com/)
[![Model: ACE-Step](https://img.shields.io/badge/Model-ACE--Step-purple.svg)](https://github.com/ace-step/ACE-Step)
[![Audio-to-MIDI: Basic Pitch](https://img.shields.io/badge/Audio--to--MIDI-Basic%20Pitch%20(Spotify)-1DB954.svg)](https://github.com/spotify/basic-pitch)

---

## ✨ What Is This?

AI DAW Copilot is a **VST3 plugin** that embeds a ChatGPT-style chat interface directly inside your DAW. Describe what you want in plain language and the plugin generates **full-quality audio stems** — then automatically extracts **MIDI** from them so you can drag either into your project.

Think of it as a Suno-style music generator that lives inside your DAW and hands you back editable MIDI.

```
"Give me a lo-fi hip hop beat with lazy drums, a Rhodes chord stab, and a walking bass line"
```

Hit send. Get audio stems *and* MIDI. Drag either straight into your DAW tracks.

---

## 🆕 What Changed from v1

| Area | v1 (old) | v2 (current) |
|------|----------|--------------|
| **Generation model** | MIDI-LLM (Llama 3.2 1B, NeurIPS 2025) | **ACE-Step** audio diffusion model |
| **Output format** | MIDI only | **Audio stems + MIDI** extracted from audio |
| **Audio quality** | Symbolic / rendered | Suno-quality realistic audio |
| **MIDI pipeline** | Direct model output | Audio-to-MIDI conversion engine (post-process) |
| **Stem isolation** | Per-instrument MIDI files | Isolate any element: full mix, drum bus, kick only, snare only, loops, etc. |
| **Tokenizer** | Anticipation AMT (MIDI tokens) | Native audio diffusion — no MIDI tokenizer |

The MIDI-LLM / Llama 3.2 1B model and the Anticipation AMT tokenizer have been **fully removed**.

---

## ⚠️ System Requirements

**Please read before installing.** The plugin runs AI models locally — generation takes several seconds depending on your hardware and is not suitable for real-time use.

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| OS | Windows 10 (64-bit) | Windows 11 (64-bit) |
| CPU | 6-core, 3.0 GHz | 8-core, 3.5 GHz+ |
| RAM | 16 GB | 32 GB |
| GPU (optional) | — | NVIDIA GPU with 6 GB+ VRAM (CUDA) for fast audio generation |
| Disk space | 4 GB free | 8 GB free |
| Internet | Required on first launch (model download) | — |
| DAW | Any VST3-compatible DAW on Windows | FL Studio, Ableton Live, Reaper |

> **Performance note:** Audio generation via ACE-Step is non-real-time. On a mid-range CPU expect 15–40 seconds per generation; a CUDA GPU brings this down significantly. The Send button disables during inference and a progress indicator is shown.

---

## 📸 Screenshots


![Workflow placeholder](docs/images/workflow_placeholder.png)
*↑ Chat interface inside DAW — prompt → audio stems → MIDI extraction → drag to tracks*

---
## 🎬 Quick Demo
---

## ⬇️ Download & Install

Head to the [**Releases**](../../releases) page and grab the latest version.

### Option A — Installer (recommended)

1. Download `AIDawCopilot_vX.X_Setup.exe`
2. Run the installer — it places the `.vst3` and sidecar in the correct locations automatically
3. Rescan plugins in your DAW
4. On **first launch**, the plugin downloads the ACE-Step model weights — a one-time internet connection is required

### Option B — Manual drag-and-drop

1. Download `AIDawCopilot_vX.X_Manual.zip`
2. Extract and copy `AI DAW Copilot.vst3` into your VST3 folder:
   ```
   C:\Program Files\Common Files\VST3\
   ```
3. Rescan plugins in your DAW
4. On **first launch**, model weights are downloaded automatically

> **DAW compatibility:** Tested in FL Studio, Ableton Live, and Reaper. Any DAW supporting VST3 on Windows should work.

---

## 🎛️ Using the Plugin

1. **Load** `AI DAW Copilot` as an instrument track in your DAW
2. **Type a prompt** describing the music you want — genre, mood, instruments, tempo, feel
3. **Hit Send** — a progress indicator shows while ACE-Step generates audio
4. When done, **stem chips** appear in the chat bubble — one per isolated element
5. Each chip offers both an **audio stem** (WAV) and an extracted **MIDI** version
6. **Drag** either variant onto any track in your DAW
7. **Refine** by continuing the conversation: *"add a synth pad"*, *"slower tempo"*, *"more swing on the drums"*

### Stem isolation options

You can ask for specific elements rather than the full mix:

| What you ask for | What you get |
|-----------------|--------------|
| Full composition | All stems + MIDI |
| "just the drums" | Full drum bus audio + MIDI |
| "kick only" | Isolated kick audio + MIDI |
| "snare only" | Isolated snare audio + MIDI |
| "bass loop" | Bass stem audio + MIDI, loop-trimmed |
| "chord stabs" | Harmonic layer audio + MIDI |

You can also ask for a single bar or 2-bar loop of any element — useful for building your own arrangement from generated building blocks.

### Prompt tips

- Be specific about instrumentation: *"Rhodes piano, upright bass, brushed snare"*
- Reference genres or feel: *"in the style of J Dilla"*, *"dark and cinematic"*
- Request loops explicitly: *"give me a 1-bar kick pattern"*, *"2-bar drum loop only"*
- Sculpt iteratively with follow-ups rather than re-describing everything from scratch
- Generation processes one request at a time — wait for the current one to finish

---

## 🔄 How It Works

The plugin is split into a **C++ VST3 front-end** (JUCE 8) and a **Python sidecar** running the AI pipeline locally. Everything stays on your machine.

```
┌─────────────────────────────────────────────────────────────┐
│                      DAW (FL, Ableton, ...)                  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │   AI DAW Copilot.vst3  (JUCE 8, C++)                  │  │
│  │                                                        │  │
│  │   Chat UI  ──▶  HttpSidecarBackend ──▶ localhost:XXXX │  │
│  │   StemSplitter ◀── audio stems + MIDI                 │  │
│  │   Drag-to-DAW (Win OLE IDataObject CF_HDROP)          │  │
│  └────────────────────────────┬──────────────────────────┘  │
└────────────────────────────────┼────────────────────────────┘
                                 │ child process, Job Object
                                 ▼
                   ┌─────────────────────────────────────┐
                   │  sidecar.exe  (PyInstaller)          │
                   │    FastAPI + Uvicorn                 │
                   │                                     │
                   │    ACE-Step                         │
                   │    (audio generation)               │
                   │         │                           │
                   │         ▼                           │
                   │    Stem isolation engine            │
                   │    (source separation / prompting)  │
                   │         │                           │
                   │         ▼                           │
                   │    Audio-to-MIDI engine             │
                   │    (per isolated stem)              │
                   │         │                           │
                   │         ▼                           │
                   │    WAV + MIDI pairs returned        │
                   └─────────────────────────────────────┘
```

### Pipeline steps

1. You type a prompt in the chat panel inside your DAW
2. The C++ plugin forwards it over localhost HTTP to the Python sidecar
3. **ACE-Step** generates a full audio mix from your prompt
4. The **stem isolation engine** separates the mix into the requested elements (full mix, drum bus, kick, snare, bass, etc.)
5. The **audio-to-MIDI conversion engine** processes each isolated audio stem and extracts a corresponding MIDI file
6. Each element is returned as a WAV + MIDI pair and rendered as a draggable chip in the chat bubble
7. Follow-up prompts append a continuation directive to the prior conversation, keeping musical context across turns

---

## 🧠 Models & Research

### Audio Generation — ACE-Step

ACE-Step is the core generative model. It produces high-quality, realistic audio from text prompts in the style of modern AI music generators.

| Resource | Link |
|----------|------|
| 🎵 ACE-Step | [GitHub — ace-step/ACE-Step](https://github.com/ace-step/ACE-Step) |

### Audio-to-MIDI Conversion — Basic Pitch (Spotify)

Each isolated stem is processed by **Basic Pitch**, Spotify's open-source automatic music transcription library. It is instrument-agnostic, supports polyphonic recordings, includes pitch bend detection, and runs faster than real time on most modern CPUs — making it a natural fit for the sidecar pipeline.

| Resource | Link |
|----------|------|
| 🎵 Basic Pitch — GitHub | [github.com/spotify/basic-pitch](https://github.com/spotify/basic-pitch) |
| 📄 Basic Pitch — paper (ICASSP 2022) | [A Lightweight Instrument-Agnostic Model for Polyphonic Note Transcription](https://arxiv.org/abs/2206.09916) |
| 🤗 Basic Pitch — Hugging Face | [huggingface.co/spotify/basic-pitch](https://huggingface.co/spotify/basic-pitch) |

### Supporting Libraries

| Resource | Link |
|----------|------|
| 🔊 JUCE framework | [juce.com](https://juce.com/) |
| ⚡ FastAPI | [fastapi.tiangolo.com](https://fastapi.tiangolo.com/) |
| 🎹 mido — MIDI I/O | [mido.readthedocs.io](https://mido.readthedocs.io/) |

### Why local?

Privacy, no latency after first load, no subscription. Your musical ideas never leave your machine.

---

## 🛠️ Building from Source

> You only need this if you want to **contribute or modify** the plugin. Regular users should grab the installer from Releases.

### Prerequisites

**VST3 build:**

| Tool | Notes |
|------|-------|
| Visual Studio 2022 Build Tools | "Desktop development with C++" workload |
| CMake | Must be in `PATH` |
| Git | Must be in `PATH` |
| Ninja | Expected at `C:\Windows\System32\ninja.exe` |
| Inno Setup **5** | ⚠️ NOT version 6 — see [pitfall notes](docs/pitfalls.md) |

**Python sidecar:**

| Tool | Notes |
|------|-------|
| Python 3.11 (x64) | Must be in `PATH` |
| ~12 GB free disk | PyInstaller intermediates + ACE-Step weights + audio-to-MIDI model |
| CUDA toolkit (optional) | For GPU-accelerated generation |

### Build steps

```bat
:: 1. Build the VST3
build.bat

:: 2. Freeze the Python sidecar into sidecar\dist\sidecar\sidecar.exe
build_sidecar.bat

:: 3. Package everything into a single installer .exe
make_installer.bat
```

### Project structure

```
AIDawCopilot/
  CMakeLists.txt
  build.bat                          # VST3 build
  build_sidecar.bat                  # Python freeze
  installer.iss                      # Inno Setup 5 script (ASCII only)
  make_installer.bat                 # Installer build

  src/
    plugin/
      PluginProcessor.{h,cpp}        # AudioProcessor, APVTS, session dir
      Parameters.h                   # automatable parameters only
    gui/
      PluginEditor.{h,cpp}           # main window
      ChatView.{h,cpp}               # scrollable message list
      MessageBubble.{h,cpp}          # one bubble, owns stem chips
      PromptInput.{h,cpp}            # multiline input + send button
      StemStrip.{h,cpp}              # draggable stem chip (audio + MIDI variant)
      LookAndFeel.{h,cpp}            # clean dark palette
      Colours.h
    inference/
      InferenceBackend.h             # abstract interface
      HttpSidecarBackend.{h,cpp}     # concrete impl over localhost HTTP
      SidecarManager.{h,cpp}         # spawn/kill Python process
    midi/
      DragSourceWindows.{h,cpp}      # OLE CF_HDROP drag source (WAV + MIDI)
    chat/
      ChatHistory.{h,cpp}            # per-instance log + XML roundtrip
      Message.h

  sidecar/
    main.py                          # FastAPI + ACE-Step + stem isolation + audio-to-MIDI
    requirements.txt
    sidecar.spec                     # PyInstaller spec
```

---

## ⚙️ Technical Notes

| Topic | Detail |
|-------|--------|
| **First-launch model download** | ACE-Step weights and the audio-to-MIDI model download on first run |
| **Generation is blocking** | ACE-Step runs one request at a time. UI shows a progress indicator and disables Send during inference |
| **Stem isolation** | Performed post-generation; users can request the full mix, a specific bus, or individual elements (kick, snare, bass, etc.) |
| **Audio-to-MIDI** | Powered by **Basic Pitch** (Spotify, Apache 2.0); applied per isolated stem after separation. MIDI accuracy is highest on clean, isolated mono sources (kick, bass) and degrades on dense polyphonic layers |
| **Drag targets** | Each chip exposes both a WAV and a MIDI drag handle — drop either onto a DAW track |
| **Iterative refinement** | Implemented by flattening prior conversation into a continuation directive appended to each new prompt |
| **Sidecar process lifetime** | Managed via Windows Job Object — sidecar is killed automatically if the DAW crashes or exits |
| **Windows only (v1)** | Drag-to-DAW uses Win32 OLE `IDataObject CF_HDROP`. Cross-platform drag support is a v2 item |
| **GPU acceleration** | ACE-Step will use CUDA automatically if a compatible GPU is available; falls back to CPU otherwise |

---

## 🤝 Contributing

Contributions are welcome! Please open an issue first for significant changes.

Areas especially open for contribution:
- macOS / Linux port (replacing the OLE drag source)
- Improved stem isolation strategies
- Better audio-to-MIDI accuracy for polyphonic sources (chords, pads)
- Loop detection and automatic loop-point trimming
- Real-time progress streaming from sidecar to UI

---

## 📄 License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

ACE-Step model weights are subject to their own license — see their repository. Basic Pitch is released under the Apache 2.0 License.

---

## 🙏 Acknowledgements

- [ACE-Step](https://github.com/ace-step/ACE-Step) for the audio generation model
- [Basic Pitch](https://github.com/spotify/basic-pitch) (Spotify Audio Intelligence Lab) for the audio-to-MIDI transcription engine
- [JUCE](https://juce.com/) for the VST3 / audio plugin framework
- [FastAPI](https://fastapi.tiangolo.com/) and [Uvicorn](https://www.uvicorn.org/) for the sidecar HTTP layer
- [mido](https://mido.readthedocs.io/) for MIDI file I/O
