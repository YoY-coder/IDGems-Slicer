#pragma once

#include <juce_core/juce_core.h>

class IDGemsSlicerProcessor;

//==============================================================================
// Presets IDGems Slicer (.idgslicer) : etat complet compresse, avec ou sans
// les fichiers audio embarques (re-extraits au chargement).
//==============================================================================
namespace PresetManager
{
    juce::File folder (const IDGemsSlicerProcessor&);
    juce::Array<juce::File> list (const IDGemsSlicerProcessor&);

    bool save (IDGemsSlicerProcessor&, const juce::File&, bool embedSamples, juce::String& error);
    bool load (IDGemsSlicerProcessor&, const juce::File&, juce::String& error);

    // Preset suivant / precedent dans le dossier (direction +1 / -1)
    juce::File neighbour (const IDGemsSlicerProcessor&, int direction);
}
