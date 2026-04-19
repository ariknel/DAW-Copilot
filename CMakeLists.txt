cmake_minimum_required(VERSION 3.22)
project(AIMidiComposer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AIMidiComposerVST
    COMPANY_NAME            "Arik"
    PLUGIN_MANUFACTURER_CODE ArKC
    PLUGIN_CODE             AiMc
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "AI MIDI Composer"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       TRUE
    IS_MIDI_EFFECT          TRUE
    VST3_CATEGORIES         "Fx|Tools"
    PLUGIN_DESCRIPTION      "Chat-based MIDI composition powered by a local AI model"
    BUNDLE_ID               "com.arik.AIMidiComposer"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(AIMidiComposerVST PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/Parameters.h

    src/gui/PluginEditor.cpp
    src/gui/PluginEditor.h
    src/gui/ChatView.cpp
    src/gui/ChatView.h
    src/gui/MessageBubble.cpp
    src/gui/MessageBubble.h
    src/gui/PromptInput.cpp
    src/gui/PromptInput.h
    src/gui/StemStrip.cpp
    src/gui/StemStrip.h
    src/gui/LookAndFeel.cpp
    src/gui/LookAndFeel.h
    src/gui/Colours.h

    src/inference/InferenceBackend.h
    src/inference/HttpSidecarBackend.cpp
    src/inference/HttpSidecarBackend.h
    src/inference/SidecarManager.cpp
    src/inference/SidecarManager.h

    src/midi/MidiStemSplitter.cpp
    src/midi/MidiStemSplitter.h
    src/midi/DragSourceWindows.cpp
    src/midi/DragSourceWindows.h

    src/chat/ChatHistory.cpp
    src/chat/ChatHistory.h
    src/chat/Message.h
)

target_include_directories(AIMidiComposerVST PRIVATE src)

target_link_libraries(AIMidiComposerVST PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(AIMidiComposerVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

if(MSVC)
    target_compile_options(AIMidiComposerVST PRIVATE /bigobj /MP)
endif()

if(WIN32)
    target_link_libraries(AIMidiComposerVST PRIVATE
        ole32
        shell32
        user32
    )
endif()
