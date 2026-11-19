#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/SampleData.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Carte des slices : chaque slice des 4 samples est un point (couleur de son
// sample). Curseur Slice Select deplacable, rayon, trajets dessines.
//==============================================================================
class MapView final : public juce::Component
{
public:
    explicit MapView (IDGemsSlicerProcessor&);
    ~MapView() override;

    void refresh();     // relit les samples (apres chargement / re-decoupe)
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Plot;
    IDGemsSlicerProcessor& processor;
    std::unique_ptr<Plot> plot;

    juce::ComboBox axisXBox, axisYBox, pathLengthBox, pathModeBox;
    juce::Label axisXLabel, axisYLabel, radiusLabel, pathLengthLabel, pathModeLabel;
    juce::Slider radiusSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ToggleButton pathToggle { "TRAJET" }, spreadToggle { "ETALER" };
    juce::TextButton drawButton { "DESSINER" }, clearButton { "EFFACER" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> axisXAtt, axisYAtt, pathLengthAtt, pathModeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> radiusAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pathAtt, spreadAtt;
};
