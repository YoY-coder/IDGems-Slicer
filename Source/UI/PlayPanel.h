#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class IDGemsSlicerProcessor;

//==============================================================================
// Panneau JEU : mode (Clavier / Scan / Accord), reglages de jeu, Scan,
// Accord, repartition reelle des probabilites, dernier declenchement.
//==============================================================================
class PlayPanel final : public juce::Component,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PlayPanel (IDGemsSlicerProcessor&);
    ~PlayPanel() override;

    void tick (const std::array<float, 4>& normalisedProbs, const std::array<bool, 4>& loaded);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void parameterChanged (const juce::String&, float) override;
    void updateEnablement();

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Row
    {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<SliderAtt> attachment;
    };

    juce::Slider& addSlider (const juce::String& paramId, const juce::String& label, const juce::String& tooltip);
    Row* findRow (const juce::String& paramId);

    IDGemsSlicerProcessor& processor;

    juce::ComboBox modeBox, sourceBox;
    juce::Label modeLabel, sourceLabel;
    std::unique_ptr<ComboAtt> modeAtt, sourceAtt;

    std::vector<std::pair<juce::String, std::unique_ptr<Row>>> rows;

    juce::ToggleButton oneShotToggle  { "ONE-SHOT" };
    juce::ToggleButton noRepeatToggle { "PAS DE REPETITION" };
    juce::ToggleButton pitchFollowToggle { "PITCH SUIT LA NOTE" };
    std::unique_ptr<ButtonAtt> oneShotAtt, noRepeatAtt, pitchFollowAtt;

    juce::Rectangle<int> scanHeader, chordHeader, distributionArea, lastTriggerArea;
    std::array<float, 4> probs {};
    std::array<bool, 4> slotLoaded {};
    int lastTriggerCount = -1;
    float triggerFlash = 0.0f;
};
