#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../chat/Message.h"

namespace AIMC {

/** Displays one stem as a draggable chip. Left-drag = WAV, right-click = MIDI. */
class StemStrip : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit StemStrip(const Stem& s);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    Stem  m_stem;
    bool  m_hovered = false;
    bool  m_dragStarted = false;
};

} // namespace AIMC
