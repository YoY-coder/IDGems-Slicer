#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/Sequencer.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Sequenceur : reglages globaux + une ligne par lane (on/off, longueur,
// random) + grille dessinable a la souris. Chaque lane a sa propre longueur.
//==============================================================================
class SequencerView final : public juce::Component
{
public:
    explicit SequencerView (IDGemsSlicerProcessor&);
    ~SequencerView() override;

    void refresh();     // relit le pattern (apres chargement d'un projet)
    void refreshIfPatternChanged();
    void tick();        // ~30 fois / s

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Grid;
    struct LaneRow
    {
        juce::ToggleButton enabled;
        juce::Slider length { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::TextButton random { "RND" };
    };

    IDGemsSlicerProcessor& processor;
    std::unique_ptr<Grid> grid;
    std::array<std::unique_ptr<LaneRow>, idg::numLanes> laneRows;

    juce::ToggleButton seqOnToggle { "SEQUENCEUR" };
    juce::ComboBox rateBox, grooveBox;
    juce::Slider swingSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label rateLabel, swingLabel, grooveLabel;
    juce::TextButton randomAllButton { "RANDOM TOUT" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqOnAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAtt, grooveAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt;

    juce::Random random;
    juce::int64 lastHeaderStep = -2;
    int shownPattern = -1;
    idg::Pattern shownCopy;
    juce::Rectangle<int> namesArea;
};
