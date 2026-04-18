# AI MIDI Composer

A VST3 plugin with a ChatGPT-style interface that generates multitrack MIDI
compositions from natural-language prompts. Runs the **MIDI-LLM** model
(NeurIPS AI4Music Workshop 2025) entirely locally.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      DAW (FL, Ableton, ...)                  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │   AI MIDI Composer.vst3  (JUCE 8, C++)                │  │
│  │                                                        │  │
│  │   Chat UI  ──▶  HttpSidecarBackend ──▶ localhost:XXXX │  │
│  │   MidiStemSplitter ◀── combined .mid                  │  │
│  │   Drag-to-DAW (Win OLE IDataObject CF_HDROP)          │  │
│  └────────────────────────────┬──────────────────────────┘  │
└────────────────────────────────┼──────────────────────────────┘
                                 │ child process, Job Object
                                 ▼
                   ┌─────────────────────────────────┐
                   │  sidecar.exe  (PyInstaller)     │
                   │    FastAPI + Uvicorn            │
                   │    MIDI-LLM (Llama 3.2 1B)      │
                   │    Anticipation (AMT tokenizer) │
                   │    mido (MIDI I/O)              │
                   └─────────────────────────────────┘
```

## Build Order

1. **`build.bat`** → compiles the VST3 (CMake + Ninja + MSVC)
2. **`build_sidecar.bat`** → creates venv, installs torch/transformers, freezes
   into `sidecar\dist\sidecar\sidecar.exe` via PyInstaller
3. **`make_installer.bat`** → Inno Setup 5 → single `.exe` installer

## Prerequisites

Same as your existing JUCE pipeline:

| Tool | Notes |
|------|-------|
| Visual Studio 2022 Build Tools | "Desktop development with C++" |
| CMake                          | in PATH |
| Git                            | in PATH |
| Ninja                          | `C:\Windows\System32\ninja.exe` |
| Inno Setup **5**               | NOT 6 — see pitfall doc |

Additional for the sidecar:

| Tool | Notes |
|------|-------|
| Python 3.11 | x64, in PATH |
| ~8 GB free disk | for PyInstaller intermediates |

## Project Layout

```
AIMidiComposer/
  CMakeLists.txt
  build.bat                          # VST3 build
  build_sidecar.bat                  # Python freeze
  installer.iss                      # Inno Setup 5 script (ASCII only)
  make_installer.bat                 # Installer build

  src/
    plugin/
      PluginProcessor.{h,cpp}       # AudioProcessor, APVTS, session dir
      Parameters.h                  # only automatable params
    gui/
      PluginEditor.{h,cpp}          # main window
      ChatView.{h,cpp}              # scrollable message list
      MessageBubble.{h,cpp}         # one bubble, owns stem chips
      PromptInput.{h,cpp}           # multiline input + send
      StemStrip.{h,cpp}             # draggable stem chip
      LookAndFeel.{h,cpp}           # clean dark palette
      Colours.h
    inference/
      InferenceBackend.h            # abstract interface
      HttpSidecarBackend.{h,cpp}    # concrete impl over localhost HTTP
      SidecarManager.{h,cpp}        # spawn/kill Python process
    midi/
      MidiStemSplitter.{h,cpp}      # multitrack MIDI -> per-instrument .mid
      DragSourceWindows.{h,cpp}     # OLE CF_HDROP drag source
    chat/
      ChatHistory.{h,cpp}           # per-instance log + XML roundtrip
      Message.h

  sidecar/
    main.py                          # FastAPI + MIDI-LLM + MIDI analysis
    requirements.txt
    sidecar.spec                     # PyInstaller
```

## Known Tradeoffs

See `docs/README.txt` for user-facing notes.  Technical:

- **Model download on first launch** — installer stays small (~150 MB) at
  the cost of an internet-dependent first run. Bundled weights were rejected
  to keep the `.exe` under the GitHub Releases 2 GB limit.
- **Python sidecar instead of embedded llama.cpp** — MIDI-LLM uses a custom
  55 k MIDI-token vocabulary not yet converted to GGUF. Shipping Python keeps
  the model intact on day one. Migrating to embedded llama.cpp is a v2 task
  that does not require any VST UI changes (InferenceBackend abstraction).
- **Generation blocks** — MIDI-LLM is autoregressive; one request at a time.
  UI shows a progress indicator; Send button disables during generation.
- **Iterative refinement** — no native edit API. Implemented by flattening
  prior conversation into a continuation directive appended to each new
  prompt. Works well for "make the drums busier" style asks.
