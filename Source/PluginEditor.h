#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/IDGemsLookAndFeel.h"
#include "UI/MainView.h"

//==============================================================================
// Fenetre du plugin : redimensionnable (poignee en bas a droite), ratio
// conserve. Le contenu (MainView) est dessine a taille fixe puis mis a
// l'echelle. La taille est memorisee dans l'etat du plugin (projet Ableton).
//==============================================================================
class IDGemsSlicerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit IDGemsSlicerEditor (IDGemsSlicerProcessor&);
    ~IDGemsSlicerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void restoreSavedSize();

private:
    IDGemsSlicerProcessor& processor;
    IDGemsLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    MainView view;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSlicerEditor)
};
