#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

class IDGemsSlicerProcessor;

//==============================================================================
// Exports (etape 9). Tout le travail lourd se fait sur un thread separe ;
// le resultat est rendu sur le thread message.
//==============================================================================
namespace Exporter
{
    using Done = std::function<void (juce::Array<juce::File> files, juce::String message)>;

    juce::File uniqueFile (const juce::File& folder, const juce::String& prefix, const juce::String& extension);
    bool writeWav (const juce::AudioBuffer<float>&, double sampleRate, const juce::File&);

    // Rendu du pattern sur N mesures (WAV et/ou MIDI des slices jouees)
    void renderPattern (IDGemsSlicerProcessor& source, int bars, bool audio, bool midi, Done done);

    // Toutes les slices des samples charges, un fichier par slice
    void exportSlices (IDGemsSlicerProcessor& source, Done done);

    // Enregistrement du resample : ecriture du fichier sur un thread separe
    void saveRecording (juce::AudioBuffer<float> audio, double sampleRate, juce::File folder, Done done);
}
