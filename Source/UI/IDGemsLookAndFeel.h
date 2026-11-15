#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Style commun de tous les controles du plugin.
//==============================================================================
class IDGemsLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    IDGemsLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawCornerResizer (juce::Graphics&, int w, int h,
                            bool isMouseOver, bool isMouseDragging) override;

    juce::Font getLabelFont (juce::Label&) override;

    static juce::Font monoFont (float height);
};
