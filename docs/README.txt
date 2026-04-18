AI MIDI Composer v0.1.0
========================

Chat-based MIDI composition, running a local AI model (MIDI-LLM, 1.4B params).

WHAT IT IS
----------
A VST3/AU plugin with a ChatGPT-style interface. You type what music you want;
the plugin generates a multi-track MIDI composition you can drag straight onto
your DAW tracks.

ARCHITECTURE
------------
  AI MIDI Composer.vst3        - the plugin (loaded by your DAW)
  sidecar\sidecar.exe          - local HTTP inference server
                                 (auto-started by the plugin, localhost only)

The plugin and sidecar talk over 127.0.0.1 only. No data leaves your machine.

FIRST RUN
---------
The first time you open the plugin in a DAW, it will:
  1. Start the sidecar (seconds)
  2. Download the MIDI-LLM model from HuggingFace (~3 GB, one-time)
  3. Load the model into memory (10-30 seconds on GPU, longer on CPU)

Weights are cached in:  %APPDATA%\AIMidiComposer\models

SYSTEM REQUIREMENTS
-------------------
  Minimum:   Windows 10, 8 GB RAM, 6 GB free disk, any CPU (slow)
  Recommended: NVIDIA GPU with 6+ GB VRAM (RTX 3060 or better)

DAW COMPATIBILITY
-----------------
Tested: FL Studio 21, Ableton Live 12, REAPER 7, Cubase 13, Bitwig 5.
Any VST3-compatible DAW should work.

DRAG-TO-DAW
-----------
Generated MIDI stems appear as draggable chips in chat bubbles. Click and
drag a chip onto any MIDI track in your DAW.

UNINSTALL
---------
Control Panel > Programs > AI MIDI Composer > Uninstall.
Model cache in %APPDATA% is NOT removed automatically (may contain data
you want to keep). Delete manually if desired.

TROUBLESHOOTING
---------------
- "Model not ready" for >5 min:    download in progress; check status bar
- Plugin not appearing in DAW:     rescan VST3 folder in DAW settings
- Firewall popup:                  approve localhost access for sidecar.exe
- Generation very slow (>60s):     running on CPU; install CUDA torch wheel

SUPPORT
-------
Built on:
  - MIDI-LLM (Wu, Kim, Huang - NeurIPS AI4Music Workshop 2025)
  - JUCE 8, Anticipatory Music Transformer, Llama 3.2 1B

Model license: CC BY-NC-SA 4.0 (non-commercial only)
