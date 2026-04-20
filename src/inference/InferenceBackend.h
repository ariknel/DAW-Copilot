#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace AIMC {

/** Abstract inference backend. v1 = Python sidecar over localhost HTTP.
    v2 could add: embedded llama.cpp, cloud API fallback, etc. */
class InferenceBackend
{
public:
    virtual ~InferenceBackend() = default;

    /** Readiness is async - backend is "available" when the model is loaded.
        Poll this before enabling the Generate button. */
    enum class Status { NotStarted, Starting, DownloadingModel, LoadingModel, Ready, Failed };

    struct StatusUpdate {
        Status       status = Status::NotStarted;
        float        progress01 = 0.f;  // 0..1 for download/load progress
        juce::String message;           // human-readable detail
    };

    using StatusCallback = std::function<void(const StatusUpdate&)>;

    /** Result of a generation request.
        v2: wavBytes contains audio, midiBytes contains Basic Pitch transcription. */
    struct GenerationResult {
        bool                         success = false;
        juce::String                 errorMessage;
        juce::MemoryBlock            wavBytes;           // WAV audio (new in v2)
        juce::MemoryBlock            combinedMidiBytes;  // MIDI from Basic Pitch
        juce::String                 detectedKey;
        juce::String                 detectedTempo;
        juce::String                 detectedTimeSig;
        juce::String                 assistantSummary;
        double                       generationSeconds = 0.0;
    };

    using ProgressCallback = std::function<void(float progress01, juce::String msg)>;
    using DoneCallback     = std::function<void(GenerationResult)>;

    /** Kick off the backend (may download/load model). Non-blocking. */
    virtual void start(StatusCallback onStatus) = 0;

    /** Stop the backend cleanly. */
    virtual void stop() = 0;

    struct Request {
        juce::String prompt;
        float        guidanceScale = 7.0f;
        float        duration      = 0.0f;  // 0 = infer from bars+BPM in prompt
    };

    virtual void generate(Request req,
                          ProgressCallback onProgress,
                          DoneCallback onDone) = 0;

    virtual Status currentStatus() const = 0;
};

} // namespace AIMC
