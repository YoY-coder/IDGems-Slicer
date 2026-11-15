#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

//==============================================================================
// IDGems Slicer - moteur audio.
//
// ETAPE 1 : squelette. Le moteur contient seulement un petit synthe sinus de
// test pour verifier que le MIDI entre et que l'audio sort. Il sera remplace
// par le vrai moteur de slices a l'etape 2.
//
// Regles audio (valables pour toutes les etapes) :
//  - aucune allocation memoire / aucun acces disque dans processBlock()
//  - tout ce qui est lourd est prepare sur un autre thread puis echange
//    de facon atomique.
//==============================================================================
class IDGemsSlicerProcessor final : public juce::AudioProcessor
{
public:
    IDGemsSlicerProcessor();
    ~IDGemsSlicerProcessor() override = default;

    //== Audio ================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //== Editeur ==============================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //== Infos ================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //== Programmes (geres plus tard par notre propre systeme de presets) =====
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //== Sauvegarde dans le projet Ableton / presets ===========================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== Acces pour l'interface ===============================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // Taille de la fenetre memorisee avec le projet
    static constexpr int baseWidth  = 1280;
    static constexpr int baseHeight = 800;
    juce::Point<int> getSavedEditorSize() const;
    void setSavedEditorSize (int w, int h);

    // Niveaux de sortie (lus par le vu-metre, ecrits par le thread audio)
    float getOutputLevel (int channel) const noexcept { return outputLevel[(size_t) juce::jlimit (0, 1, channel)].load(); }

    // Activite MIDI (clignotant dans l'interface)
    std::atomic<int> midiActivityCounter { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* outputGainDb = nullptr;
    juce::SmoothedValue<float> outputGain;

    juce::Synthesiser testSynth;   // ETAPE 1 seulement

    std::array<std::atomic<float>, 2> outputLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSlicerProcessor)
};
