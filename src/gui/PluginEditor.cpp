#include "PluginEditor.h"
#include "Colours.h"
// MidiStemSplitter removed in v2 - ACE-Step produces WAV audio directly

namespace AIMC {

// ============================================================
//  Construction
// ============================================================

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p),
      m_proc(p),
      m_chat(p.getHistory()),
      m_input()
{
    setLookAndFeel(&m_lnf);

    // ---- Header ----
    m_title.setText("AI MIDI Composer", juce::dontSendNotification);
    m_title.setFont(juce::Font(juce::FontOptions(14.f).withStyle("Bold")));
    m_title.setColour(juce::Label::textColourId, Colours::textPrimary);
    m_title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_title);

    m_statusLine.setFont(juce::Font(juce::FontOptions(11.f)));
    m_statusLine.setColour(juce::Label::textColourId, Colours::textSecondary);
    m_statusLine.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_statusLine);

    m_progressBar.setPercentageDisplay(false);
    m_progressBar.setColour(juce::ProgressBar::foregroundColourId, Colours::accent);
    m_progressBar.setColour(juce::ProgressBar::backgroundColourId, Colours::divider);
    addChildComponent(m_progressBar);

    auto setupHeaderBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
        btn.setColour(juce::TextButton::textColourOffId, Colours::textSecondary);
        btn.setColour(juce::TextButton::buttonOnColourId, Colours::accent);
        addAndMakeVisible(btn);
    };
    setupHeaderBtn(m_settingsBtn);
    setupHeaderBtn(m_logBtn);
    setupHeaderBtn(m_clearBtn);

    m_settingsBtn.onClick = [this] { showPanel(m_activePanel == 1 ? 0 : 1); };
    m_logBtn.onClick      = [this] { showPanel(m_activePanel == 2 ? 0 : 2); };
    m_clearBtn.onClick    = [this] {
        m_proc.getHistory().clear();
        m_chat.refresh();
    };

    // ---- Quick actions ----
    auto setupQuick = [this](juce::TextButton& btn, juce::String prompt) {
        btn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
        btn.setColour(juce::TextButton::textColourOffId, Colours::accent);
        btn.onClick = [this, prompt] { insertQuickPrompt(prompt); };
        addAndMakeVisible(btn);
    };
    setupQuick(m_qBassline,   "Add a driving bassline that fits the current piece");
    setupQuick(m_qDrums,      "Add a drum pattern that matches the style and tempo");
    setupQuick(m_qMelody,     "Add a melodic lead that complements the harmony");
    setupQuick(m_qChords,     "Add chord pads or comping that supports the melody");
    setupQuick(m_qVariation,  "Create a variation of the last piece with different feel");
    setupQuick(m_qContinue,   "Continue and extend the last piece by 8 more bars");

    // ---- Chat + input ----
    addAndMakeVisible(m_chat);
    addAndMakeVisible(m_input);

    // ---- Settings panel ----
    m_spTitle.setText("Settings", juce::dontSendNotification);
    m_spTitle.setFont(juce::Font(juce::FontOptions(13.f).withStyle("Bold")));
    m_spTitle.setColour(juce::Label::textColourId, Colours::textPrimary);

    m_spStatusLabel.setFont(juce::Font(juce::FontOptions(11.f)));
    m_spStatusLabel.setColour(juce::Label::textColourId, Colours::textSecondary);
    m_spStatusLabel.setJustificationType(juce::Justification::topLeft);

    auto setupSliderLabel = [this](juce::Label& lbl, const juce::String& text) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions(11.f)));
        lbl.setColour(juce::Label::textColourId, Colours::textSecondary);
        m_settingsPanel.addAndMakeVisible(lbl);
    };
    setupSliderLabel(m_spTempLabel,   "Temperature");
    setupSliderLabel(m_spTopPLabel,   "Top-P");
    setupSliderLabel(m_spTokensLabel, "Max Tokens");

    auto setupSlider = [this](juce::Slider& s, double lo, double hi, double def, double step) {
        s.setRange(lo, hi, step);
        s.setValue(def);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        s.setColour(juce::Slider::thumbColourId, Colours::accent);
        s.setColour(juce::Slider::trackColourId, Colours::divider);
        s.setColour(juce::Slider::textBoxTextColourId, Colours::textSecondary);
        s.setColour(juce::Slider::textBoxBackgroundColourId, Colours::bg);
        s.setColour(juce::Slider::textBoxOutlineColourId, Colours::divider);
        m_settingsPanel.addAndMakeVisible(s);
    };
    setupSlider(m_spTempSlider,   0.1, 1.5, 0.9,  0.01);
    setupSlider(m_spTopPSlider,   0.1, 1.0, 0.98, 0.01);
    setupSlider(m_spTokensSlider, 256, 4096, 2000, 1.0);

    // v2: temperature/topP/maxTokens removed - ACE-Step uses guidance_scale only
    // Sliders kept in UI for future use but not wired to APVTS

    m_spRestartBtn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
    m_spRestartBtn.setColour(juce::TextButton::textColourOffId, Colours::accent);
    m_spRestartBtn.onClick = [this] { restartSidecar(); };

    m_spCloseBtn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
    m_spCloseBtn.setColour(juce::TextButton::textColourOffId, Colours::textSecondary);
    m_spCloseBtn.onClick = [this] { showPanel(0); };

    m_spLogPathLabel.setFont(juce::Font(juce::FontOptions(10.f)));
    m_spLogPathLabel.setColour(juce::Label::textColourId, Colours::textMuted);
    m_spLogPathLabel.setJustificationType(juce::Justification::bottomLeft);
    {
        auto logPath = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("AIMidiComposer").getChildFile("sidecar.log");
        m_spLogPathLabel.setText("Log: " + logPath.getFullPathName(), juce::dontSendNotification);
    }

    m_spVersionLabel.setText("AI MIDI Composer v0.1  |  MIDI-LLM (Llama 3.2 1B)", juce::dontSendNotification);
    m_spVersionLabel.setFont(juce::Font(juce::FontOptions(10.f)));
    m_spVersionLabel.setColour(juce::Label::textColourId, Colours::textMuted);
    m_spVersionLabel.setJustificationType(juce::Justification::bottomLeft);

    m_settingsPanel.addAndMakeVisible(m_spTitle);
    m_settingsPanel.addAndMakeVisible(m_spStatusLabel);
    m_settingsPanel.addAndMakeVisible(m_spRestartBtn);
    m_settingsPanel.addAndMakeVisible(m_spCloseBtn);
    m_settingsPanel.addAndMakeVisible(m_spLogPathLabel);
    m_settingsPanel.addAndMakeVisible(m_spVersionLabel);

    m_settingsPanel.setVisible(false);
    addAndMakeVisible(m_settingsPanel);

    // ---- Log panel ----
    m_lpTitle.setText("Sidecar Log", juce::dontSendNotification);
    m_lpTitle.setFont(juce::Font(juce::FontOptions(13.f).withStyle("Bold")));
    m_lpTitle.setColour(juce::Label::textColourId, Colours::textPrimary);

    m_lpText.setMultiLine(true);
    m_lpText.setReadOnly(true);
    m_lpText.setScrollbarsShown(true);
    m_lpText.setFont(juce::Font(juce::FontOptions(10.f)));
    m_lpText.setColour(juce::TextEditor::backgroundColourId, Colours::bg);
    m_lpText.setColour(juce::TextEditor::textColourId, Colours::textSecondary);
    m_lpText.setColour(juce::TextEditor::outlineColourId, Colours::divider);

    m_lpCopyBtn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
    m_lpCopyBtn.setColour(juce::TextButton::textColourOffId, Colours::textSecondary);
    m_lpCopyBtn.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(m_logBuffer);
    };

    m_lpCloseBtn.setColour(juce::TextButton::buttonColourId, Colours::bgAlt);
    m_lpCloseBtn.setColour(juce::TextButton::textColourOffId, Colours::textSecondary);
    m_lpCloseBtn.onClick = [this] { showPanel(0); };

    m_logPanel.addAndMakeVisible(m_lpTitle);
    m_logPanel.addAndMakeVisible(m_lpText);
    m_logPanel.addAndMakeVisible(m_lpCopyBtn);
    m_logPanel.addAndMakeVisible(m_lpCloseBtn);

    m_logPanel.setVisible(false);
    addAndMakeVisible(m_logPanel);

    // ---- Callbacks ----
    p.getHistory().onChanged = [safe = m_safe]() {
        if (safe == nullptr) return;
        juce::MessageManager::callAsync([safe]() {
            if (safe != nullptr) safe->m_chat.refresh();
        });
    };

    m_input.onSubmit = [this](juce::String txt) { submitPrompt(std::move(txt)); };

    // ---- Deferred backend start ----
    juce::Timer::callAfterDelay(200, [safe = m_safe]() {
        if (safe == nullptr) return;
        safe->m_proc.initBackend();
        if (auto* backend = safe->m_proc.getBackend()) {
            backend->start([safe](const InferenceBackend::StatusUpdate& u) {
                if (safe != nullptr) safe->onBackendStatus(u);
            });
        }
    });

    setSize(760, 680);
    setResizable(true, true);
    setResizeLimits(560, 460, 1800, 1400);
    startTimerHz(2);  // 2Hz - enough for smooth elapsed time display
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    m_proc.getHistory().onChanged = nullptr;
    setLookAndFeel(nullptr);
}

// ============================================================
//  Paint + Layout
// ============================================================

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(Colours::bg);
    g.setColour(Colours::divider);
    g.drawHorizontalLine(44, 0.f, (float)getWidth());
    g.drawHorizontalLine(44 + 36, 0.f, (float)getWidth());  // quick-action strip divider

    if (m_activePanel > 0) {
        g.setColour(Colours::bgAlt);
        g.fillRect(getPanelBounds());
        g.setColour(Colours::divider);
        g.drawRect(getPanelBounds(), 1);
    }
}

juce::Rectangle<int> PluginEditor::getPanelBounds() const
{
    return getLocalBounds().removeFromRight(300).withTrimmedTop(44);
}

void PluginEditor::resized()
{
    auto r = getLocalBounds();

    // Header
    auto header = r.removeFromTop(44);
    m_title.setBounds(header.removeFromLeft(190).reduced(10, 0));
    m_settingsBtn.setBounds(header.removeFromRight(68).reduced(4, 6));
    m_logBtn.setBounds(header.removeFromRight(42).reduced(4, 6));
    m_clearBtn.setBounds(header.removeFromRight(48).reduced(4, 6));

    // Progress / status in remaining header space
    auto statusArea = header.reduced(8, 6);
    if (m_progressBar.isVisible())
        m_progressBar.setBounds(statusArea.removeFromRight(100).withSizeKeepingCentre(100, 10));
    m_statusLine.setBounds(statusArea);

    // Quick-action strip
    auto strip = r.removeFromTop(36).reduced(6, 4);
    int bw = strip.getWidth() / 6;
    m_qBassline.setBounds(strip.removeFromLeft(bw).reduced(2, 0));
    m_qDrums.setBounds(strip.removeFromLeft(bw).reduced(2, 0));
    m_qMelody.setBounds(strip.removeFromLeft(bw).reduced(2, 0));
    m_qChords.setBounds(strip.removeFromLeft(bw).reduced(2, 0));
    m_qVariation.setBounds(strip.removeFromLeft(bw).reduced(2, 0));
    m_qContinue.setBounds(strip.reduced(2, 0));

    // Shrink content area if a panel is open
    if (m_activePanel > 0)
        r.removeFromRight(300);

    // Input + chat
    auto inputArea = r.removeFromBottom(110);
    m_input.setBounds(inputArea);
    m_chat.setBounds(r);

    // Settings panel
    if (m_activePanel == 1) {
        auto p = getPanelBounds().reduced(14);
        m_spTitle.setBounds(p.removeFromTop(24));
        p.removeFromTop(6);
        m_spStatusLabel.setBounds(p.removeFromTop(54));
        p.removeFromTop(10);
        m_spTempLabel.setBounds(p.removeFromTop(16));
        m_spTempSlider.setBounds(p.removeFromTop(28));
        p.removeFromTop(4);
        m_spTopPLabel.setBounds(p.removeFromTop(16));
        m_spTopPSlider.setBounds(p.removeFromTop(28));
        p.removeFromTop(4);
        m_spTokensLabel.setBounds(p.removeFromTop(16));
        m_spTokensSlider.setBounds(p.removeFromTop(28));
        p.removeFromTop(12);
        m_spRestartBtn.setBounds(p.removeFromTop(30));
        p.removeFromTop(6);
        m_spCloseBtn.setBounds(p.removeFromTop(26));
        auto bottom = getPanelBounds().reduced(14).removeFromBottom(44);
        m_spVersionLabel.setBounds(bottom.removeFromBottom(18));
        m_spLogPathLabel.setBounds(bottom);
        m_settingsPanel.setBounds(getPanelBounds());
    }

    // Log panel
    if (m_activePanel == 2) {
        auto p = getPanelBounds().reduced(10);
        m_lpTitle.setBounds(p.removeFromTop(24));
        p.removeFromTop(4);
        auto btnRow = p.removeFromBottom(28);
        m_lpCopyBtn.setBounds(btnRow.removeFromLeft(70).reduced(0, 2));
        btnRow.removeFromLeft(6);
        m_lpCloseBtn.setBounds(btnRow.removeFromLeft(60).reduced(0, 2));
        m_lpText.setBounds(p.reduced(0, 4));
        m_logPanel.setBounds(getPanelBounds());
    }
}

// ============================================================
//  Panel switching
// ============================================================

void PluginEditor::showPanel(int panel)
{
    m_activePanel = panel;
    m_settingsPanel.setVisible(panel == 1);
    m_logPanel.setVisible(panel == 2);

    m_settingsBtn.setToggleState(panel == 1, juce::dontSendNotification);
    m_logBtn.setToggleState(panel == 2, juce::dontSendNotification);

    if (panel == 1) {
        juce::String txt = m_lastStatusIsError
            ? "Status: Error\n" + m_lastStatusText
            : "Status: " + m_lastStatusText;
        m_spStatusLabel.setText(txt, juce::dontSendNotification);
        m_spStatusLabel.setColour(juce::Label::textColourId,
            m_lastStatusIsError ? Colours::error : Colours::textSecondary);
    }

    resized();
    repaint();
}

// ============================================================
//  Backend status
// ============================================================

void PluginEditor::onBackendStatus(const InferenceBackend::StatusUpdate& u)
{
    using S = InferenceBackend::Status;
    m_isModelReady       = (u.status == S::Ready);
    m_lastStatusIsError  = (u.status == S::Failed);
    m_lastStatusText     = u.message;

    // Forward to log
    appendLog("[Status] " + u.message);

    // Update settings panel if open
    if (m_activePanel == 1) showPanel(1);

    switch (u.status) {
        case S::NotStarted:
            showStatusLine("Idle");
            m_input.setPlaceholder("Waiting for backend...");
            m_progressBar.setVisible(false);
            break;
        case S::Starting:
            showStatusLine("Starting AI engine...");
            m_input.setPlaceholder("Starting local inference engine...");
            m_progress = 0.0;
            m_progressBar.setVisible(true);
            break;
        case S::DownloadingModel:
            showStatusLine("Downloading model " + juce::String((int)(u.progress01 * 100)) + "%");
            m_input.setPlaceholder("Downloading AI model (~3 GB, one time only)...");
            m_progress = u.progress01;
            m_progressBar.setVisible(true);
            break;
        case S::LoadingModel:
            showStatusLine("Loading model " + juce::String((int)(u.progress01 * 100)) + "%");
            m_input.setPlaceholder("Loading model into memory...");
            m_progress = u.progress01;
            m_progressBar.setVisible(true);
            break;
        case S::Ready:
            showStatusLine(u.message.isEmpty() ? juce::String("Ready") : u.message);
            m_input.setPlaceholder("Describe music, ask for a bassline, variation, continuation...");
            m_progress = 1.0;
            m_progressBar.setVisible(false);
            m_readyStatusMsg = u.message;  // remember for setGenerating
            break;
        case S::Failed: {
            auto line1 = u.message.upToFirstOccurrenceOf("\n", false, false);
            showStatusLine(line1.isEmpty() ? juce::String("Engine failed") : line1, true);
            m_input.setPlaceholder("Inference engine unavailable. Check Log panel for details.");
            m_progressBar.setVisible(false);
            const auto& msgs = m_proc.getHistory().messages();
            bool already = (!msgs.empty() && msgs.back().isError && msgs.back().errorText == u.message);
            if (!already) {
                Message err(MessageRole::Assistant, {});
                err.isError   = true;
                err.errorText = u.message.isEmpty() ? juce::String("Sidecar failed.") : u.message;
                m_proc.getHistory().append(err);
            }
            break;
        }
    }
    resized();
}

// ============================================================
//  Timer / send button sync
// ============================================================

void PluginEditor::timerCallback()
{
    m_input.setSendEnabled(m_isModelReady && !m_isGenerating);

    // Update elapsed time display while generating
    if (m_isGenerating) {
        auto elapsed = juce::Time::currentTimeMillis() - m_generationStartMs;
        int secs  = (int)(elapsed / 1000);
        int mins  = secs / 60;
        secs     %= 60;
        juce::String timeStr = juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
        juce::String status  = "Generating...  " + timeStr;

        // Warn if taking a long time (GPU may be slow)
        if (elapsed > 120000)
            status = "Generating (GPU is working, please wait)  " + timeStr;

        showStatusLine(status);
    }
}

// ============================================================
//  Status line
// ============================================================

void PluginEditor::showStatusLine(const juce::String& s, bool isError)
{
    m_statusLine.setText(s, juce::dontSendNotification);
    m_statusLine.setColour(juce::Label::textColourId,
                           isError ? Colours::error : Colours::textSecondary);
}

// ============================================================
//  Generation state
// ============================================================

void PluginEditor::setGenerating(bool on)
{
    m_isGenerating = on;
    m_input.setSendEnabled(!on && m_isModelReady);
    if (on) {
        m_generationStartMs = juce::Time::currentTimeMillis();
        showStatusLine("Generating...  0:00");
    } else {
        showStatusLine(m_isModelReady ? (m_readyStatusMsg.isEmpty() ? juce::String("Ready") : m_readyStatusMsg) : juce::String("Idle"));
    }
}

// ============================================================
//  Quick actions
// ============================================================

void PluginEditor::insertQuickPrompt(const juce::String& text)
{
    if (!m_isModelReady || m_isGenerating) return;
    submitPrompt(text);
}

// ============================================================
//  Restart sidecar
// ============================================================

void PluginEditor::restartSidecar()
{
    m_spRestartBtn.setEnabled(false);
    m_spRestartBtn.setButtonText("Restarting...");
    m_isModelReady = false;
    showStatusLine("Restarting engine...");
    m_logBuffer.clear();
    m_lpText.clear();

    if (auto* backend = m_proc.getBackend()) {
        backend->stop();
        backend->start([safe = m_safe](const InferenceBackend::StatusUpdate& u) {
            if (safe != nullptr) safe->onBackendStatus(u);
        });
    }

    juce::Timer::callAfterDelay(2000, [safe = m_safe] {
        if (safe != nullptr) {
            safe->m_spRestartBtn.setEnabled(true);
            safe->m_spRestartBtn.setButtonText("Restart Sidecar");
        }
    });
}

// ============================================================
//  Log
// ============================================================

void PluginEditor::appendLog(const juce::String& line)
{
    m_logBuffer += line + "\n";
    if (m_activePanel == 2) {
        m_lpText.moveCaretToEnd();
        m_lpText.insertTextAtCaret(line + "\n");
    }
}

// ============================================================
//  Submit
// ============================================================

void PluginEditor::submitPrompt(juce::String rawPrompt)
{
    if (!m_isModelReady || m_isGenerating) return;

    Message userMsg(MessageRole::User, rawPrompt);
    m_proc.getHistory().append(userMsg);

    auto flattened = m_proc.getHistory().buildFlattenedPrompt(rawPrompt);

    InferenceBackend::Request req;
    req.prompt        = flattened;
    req.guidanceScale = 7.0f;   // ACE-Step guidance scale
    req.duration      = 0.0f;   // 0 = infer from bars+BPM in the prompt

    setGenerating(true);

    auto onDone = [this, safe = m_safe](InferenceBackend::GenerationResult r) {
        if (safe == nullptr) return;
        if (!r.success) {
            Message err(MessageRole::Assistant, {});
            err.isError   = true;
            err.errorText = r.errorMessage.isEmpty() ? juce::String("Unknown error") : r.errorMessage;
            m_proc.getHistory().append(err);
            setGenerating(false);
            return;
        }

        auto sessionDir = m_proc.getSessionDir();
        auto ts = juce::Time::getCurrentTime().formatted("take_%H%M%S");
        std::vector<Stem> stems;

        // Write WAV audio stem
        if (r.wavBytes.getSize() > 0) {
            auto wavFile = sessionDir.getChildFile(ts + "_audio.wav");
            wavFile.replaceWithData(r.wavBytes.getData(), r.wavBytes.getSize());
            Stem s;
            s.instrumentName = "Generated Audio";
            s.wavFile        = wavFile;
            stems.push_back(std::move(s));
        }

        // Write MIDI stem (Basic Pitch transcription)
        juce::File midiFile;
        if (r.combinedMidiBytes.getSize() > 0) {
            midiFile = sessionDir.getChildFile(ts + "_transcribed.mid");
            midiFile.replaceWithData(r.combinedMidiBytes.getData(),
                                     r.combinedMidiBytes.getSize());
            if (!stems.empty()) stems[0].midiFile = midiFile;
        }

        Message ai(MessageRole::Assistant,
                   r.assistantSummary.isEmpty()
                       ? juce::String("Generated. Drag WAV or MIDI into your DAW.")
                       : r.assistantSummary);
        ai.detectedKey     = r.detectedKey;
        ai.detectedTempo   = r.detectedTempo;
        ai.detectedTimeSig = r.detectedTimeSig;
        ai.fullMidiFile    = midiFile;
        ai.stems           = std::move(stems);
        m_proc.getHistory().append(ai);
        setGenerating(false);
    };

    m_proc.getBackend()->generate(req, [](float, juce::String) {}, onDone);
}

juce::AudioProcessorEditor* createEditorFor(PluginProcessor& p)
{
    return new PluginEditor(p);
}

} // namespace AIMC
