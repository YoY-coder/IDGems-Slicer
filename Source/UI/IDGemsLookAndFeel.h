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
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    static juce::Font monoFont (float height);

    // Police d'affichage IDGems (titres). Utilise la police deposee dans
    // Resources/Fonts si elle existe, sinon la police mono en gras.
    static juce::Font titleFont (float height);
    static bool hasCustomTitleFont();
};
