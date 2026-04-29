#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/PluginProcessor.h"
#include "LookAndFeel.h"
#include "ChatView.h"
#include "PromptInput.h"

namespace AIMC {

class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Called from HttpSidecarBackend status callback (always on message thread)
    void onBackendStatus(const InferenceBackend::StatusUpdate& u);

private:
    // --- Timer ---
    void timerCallback() override;

    // --- Actions ---
    void submitPrompt(juce::String prompt);
    void setGenerating(bool on);
    void showStatusLine(const juce::String& s, bool isError = false);
    void showPanel(int panel);   // 0=none 1=settings 2=log
    void restartSidecar();
    void insertQuickPrompt(const juce::String& text);
    void appendLog(const juce::String& line);
    juce::Rectangle<int> getPanelBounds() const;

    PluginProcessor& m_proc;
    LookAndFeel      m_lnf;

    // --- Header ---
    juce::Label      m_title;
    juce::Label      m_statusLine;
    double           m_progress = 0.0;
    juce::ProgressBar m_progressBar { m_progress };
    juce::TextButton m_settingsBtn { "Settings" };
    juce::TextButton m_logBtn      { "Log" };
    juce::TextButton m_clearBtn    { "Clear" };

    // --- Chat ---
    ChatView         m_chat;
    PromptInput      m_input;

    // --- Quick-action strip ---
    juce::TextButton m_qBassline    { "+ Bassline" };
    juce::TextButton m_qDrums      { "+ Drums" };
    juce::TextButton m_qMelody     { "+ Melody" };
    juce::TextButton m_qChords     { "+ Chords" };
    juce::TextButton m_qVariation  { "Variation" };
    juce::TextButton m_qContinue   { "Continue" };

    // --- Settings panel ---
    juce::Component  m_settingsPanel;
    juce::Label      m_spTitle;
    juce::Label      m_spStatusLabel;
    juce::Label      m_spTempLabel,  m_spTopPLabel,  m_spTokensLabel;
    juce::Slider     m_spTempSlider, m_spTopPSlider, m_spTokensSlider;
    juce::TextButton m_spRestartBtn  { "Restart Sidecar" };
    juce::TextButton m_spCloseBtn    { "Close" };
    juce::Label      m_spLogPathLabel;
    juce::Label      m_spVersionLabel;

    // --- Debug log panel ---
    juce::Component  m_logPanel;
    juce::Label      m_lpTitle;
    juce::TextEditor m_lpText;
    juce::TextButton m_lpCopyBtn  { "Copy" };
    juce::TextButton m_lpCloseBtn { "Close" };

    // --- State ---
    bool             m_isGenerating  = false;
    bool             m_isModelReady  = false;
    juce::String     m_readyStatusMsg;
    juce::int64      m_generationStartMs = 0;
    int              m_activePanel   = 0;   // 0=none 1=settings 2=log
    juce::String     m_lastStatusText;
    bool             m_lastStatusIsError = false;
    juce::String     m_logBuffer;

    juce::Component::SafePointer<PluginEditor> m_safe { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

juce::AudioProcessorEditor* createEditorFor(PluginProcessor& p);

} // namespace AIMC
