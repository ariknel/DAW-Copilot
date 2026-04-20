#include "StemStrip.h"
#include "Colours.h"
#include "../midi/DragSourceWindows.h"

namespace AIMC {

StemStrip::StemStrip(const Stem& s) : m_stem(s)
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    bool hasWav  = s.wavFile.existsAsFile();
    bool hasMidi = s.midiFile.existsAsFile();
    if (hasWav && hasMidi)
        setTooltip("Drag into DAW  (WAV audio)  -  right-click for MIDI");
    else if (hasWav)
        setTooltip("Drag into DAW (WAV audio)");
    else
        setTooltip("Drag into DAW (MIDI)");
}

void StemStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.f);
    auto bg = m_hovered ? Colours::stemChipHover : Colours::stemChipBg;
    g.setColour(bg);
    g.fillRoundedRectangle(r, 6.f);

    // Dot colour: green for WAV, blue for MIDI-only
    g.setColour(m_stem.wavFile.existsAsFile() ? Colours::accent
                                               : juce::Colour(0xff4a9eff));
    g.fillEllipse(r.getX() + 8.f, r.getCentreY() - 4.f, 8.f, 8.f);

    g.setColour(Colours::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(13.f).withStyle("SemiBold")));
    auto textArea = r.withTrimmedLeft(24.f).withTrimmedRight(4.f);
    g.drawFittedText(juce::String(m_stem.instrumentName),
                     textArea.toNearestInt(), juce::Justification::centredLeft, 1);

    // Show WAV/MIDI badge and duration
    g.setColour(Colours::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(10.f)));
    juce::String badge = m_stem.wavFile.existsAsFile() ? "WAV" : "MIDI";
    juce::String meta  = badge;
    if (m_stem.durationSeconds > 0.0)
        meta += " | " + juce::String(m_stem.durationSeconds, 1) + "s";
    g.drawFittedText(meta,
                     textArea.removeFromBottom(14).toNearestInt(),
                     juce::Justification::bottomLeft, 1);
}

void StemStrip::mouseDown(const juce::MouseEvent&)
{
    m_dragStarted = false;
}

void StemStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragStarted) return;
    if (e.getDistanceFromDragStart() < 8) return;

    // Prefer WAV; fall back to MIDI
    juce::File dragFile = m_stem.wavFile.existsAsFile()  ? m_stem.wavFile
                        : m_stem.midiFile.existsAsFile() ? m_stem.midiFile
                        : juce::File{};
    if (! dragFile.existsAsFile()) return;

    m_dragStarted = true;
    juce::StringArray paths;
    paths.add(dragFile.getFullPathName());
    WinFileDragSource::performDrag(paths);
}

void StemStrip::mouseUp(const juce::MouseEvent& e)
{
    // Right-click: if we have both WAV and MIDI, offer MIDI drag on right click
    if (e.mods.isRightButtonDown() && m_stem.midiFile.existsAsFile()) {
        juce::StringArray paths;
        paths.add(m_stem.midiFile.getFullPathName());
        WinFileDragSource::performDrag(paths);
    }
}

void StemStrip::mouseEnter(const juce::MouseEvent&) { m_hovered = true;  repaint(); }
void StemStrip::mouseExit (const juce::MouseEvent&) { m_hovered = false; repaint(); }

} // namespace AIMC
