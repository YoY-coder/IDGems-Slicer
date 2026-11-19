#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class IDGemsSlicerProcessor;

//==============================================================================
// Reglages de voix : enveloppe ADSR, pitch "clic", start / longueur / reverse,
// filtre + drive, Slice Freeze.
//==============================================================================
class VoiceView final : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit VoiceView (IDGemsSlicerProcessor&);
    ~VoiceView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& label, const juce::String& tooltip);
    void placeKnobs (juce::Rectangle<int> area, std::initializer_list<const char*> ids);
    void parameterChanged (const juce::String&, float) override;

    IDGemsSlicerProcessor& processor;
    std::vector<std::pair<juce::String, std::unique_ptr<Knob>>> knobs;

    juce::ToggleButton reverseToggle { "REVERSE" }, freezeToggle { "FREEZE ACTIF" };
    juce::ComboBox filterBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAtt, freezeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterAtt;

    juce::Rectangle<int> envArea, clickArea, playArea, filterArea, freezeArea, envGraph;
};
