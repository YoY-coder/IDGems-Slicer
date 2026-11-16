#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "WaveformView.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Editeur de decoupe du sample selectionne : grande forme d'onde + reglages.
//==============================================================================
class SliceEditor final : public juce::Component,
                          private juce::Timer
{
public:
    explicit SliceEditor (IDGemsSlicerProcessor&);
    ~SliceEditor() override;

    void setSlot (int slot);
    int getSlot() const noexcept { return slot; }
    void refresh();
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;      // anti-rebond des reglages
    void settingsChanged();
    void updateControlVisibility();

    IDGemsSlicerProcessor& processor;
    int slot = 0;
    bool updating = false;

    WaveformView wave { true };

    juce::ComboBox modeBox, divBox;
    juce::Slider countSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider sensSlider  { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label  modeLabel, countLabel, sensLabel, divLabel, fadeLabel;
    juce::TextButton randomButton { "RE-SLICE RANDOM" };
    juce::Slider fadeSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ToggleButton normalizeToggle { "NORMALISER" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fadeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normalizeAttachment;

    juce::String header;
    juce::Colour colour;
    int lastTriggerCount = 0;
};
