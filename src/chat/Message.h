#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <string>

namespace AIMC {

/** A single stem belonging to an assistant message. v2 has both WAV + MIDI. */
struct Stem {
    std::string  instrumentName;       // "Generated Audio", "Piano" etc.
    int          generalMidiProgram = 0;
    int          noteCount = 0;
    double       durationSeconds = 0.0;
    juce::File   midiFile;             // Basic Pitch MIDI transcription
    juce::File   wavFile;              // ACE-Step generated audio (v2)
};

enum class MessageRole { User, Assistant, System };

/** One turn in the conversation. */
struct Message {
    MessageRole         role = MessageRole::User;
    juce::String        text;                 // user prompt, or assistant summary
    juce::int64         unixTimeMs = 0;

    // Populated only for assistant messages that produced MIDI:
    juce::File          fullMidiFile;         // combined multitrack .mid
    std::vector<Stem>   stems;

    // Generation metadata (assistant only):
    juce::String        detectedKey;
    juce::String        detectedTempo;
    juce::String        detectedTimeSig;
    bool                isError = false;
    juce::String        errorText;

    Message() = default;
    Message(MessageRole r, juce::String t)
        : role(r), text(std::move(t)),
          unixTimeMs(juce::Time::currentTimeMillis()) {}
};

} // namespace AIMC
