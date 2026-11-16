#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class IDGemsSlicerProcessor;

//==============================================================================
// Panneau JEU / AIGUILLAGE : reglages du Mode Clavier + repartition des
// probabilites entre les 4 samples + dernier declenchement.
//==============================================================================
class PlayPanel final : public juce::Component
{
public:
    explicit PlayPanel (IDGemsSlicerProcessor&);
    ~PlayPanel() override;

    void tick (const std::array<float, 4>& normalisedProbs, const std::array<bool, 4>& loaded);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    IDGemsSlicerProcessor& processor;

    juce::ComboBox sourceBox;
    juce::Slider rootSlider      { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider polySlider      { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider transposeSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ToggleButton oneShotToggle  { "ONE-SHOT" };
    juce::ToggleButton noRepeatToggle { "PAS DE REPETITION" };
    juce::Label sourceLabel, rootLabel, polyLabel, transposeLabel;

    std::unique_ptr<ComboAtt> sourceAtt;
    std::unique_ptr<SliderAtt> rootAtt, polyAtt, transposeAtt;
    std::unique_ptr<ButtonAtt> oneShotAtt, noRepeatAtt;

    juce::Rectangle<int> distributionArea, lastTriggerArea;
    std::array<float, 4> probs {};
    std::array<bool, 4> slotLoaded {};
    int lastTriggerCount = -1;
    float triggerFlash = 0.0f;
};
