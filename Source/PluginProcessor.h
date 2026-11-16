#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine/SampleManager.h"
#include "Engine/VoiceEngine.h"
#include "Engine/Router.h"

//==============================================================================
// IDGems Slicer - moteur audio.
//
// ETAPES 2-3 : 4 emplacements de samples decoupes, aiguillage par
// probabilite, Mode Clavier (une slice par touche).
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

    //== Samples / moteur ======================================================
    idg::SampleManager& getSampleManager() noexcept { return sampleManager; }
    const idg::VoiceEngine& getVoiceEngine() const noexcept { return engine; }

    // Pre-ecoute d'une slice (appele depuis l'interface)
    void requestPreview (int slot, int slice);

    // Probabilites brutes (0..1) lues dans les parametres
    std::array<float, idg::numSlots> getProbabilities() const noexcept;

    // Dernier declenchement (pour l'affichage)
    std::atomic<int> lastTriggerSlot { -1 }, lastTriggerSlice { -1 }, triggerCounter { 0 };

    double getHostBpm() const noexcept { return hostBpm.load(); }

    int getSelectedSlot() const;
    void setSelectedSlot (int);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* outputGainDb = nullptr;
    juce::SmoothedValue<float> outputGain;

    void handleMidi (const juce::MidiMessage&);
    void triggerSlice (int slot, int slice, int note, float velocity, bool forceOneShot);

    std::atomic<double> hostBpm { 120.0 };
    idg::SampleManager sampleManager { [this] { return hostBpm.load(); } };
    idg::VoiceEngine engine;
    idg::Router router;
    std::array<idg::SampleData::Ptr, idg::numSlots> audioSlots;   // cache du thread audio

    double currentSampleRate = 44100.0;

    // Parametres
    std::array<std::atomic<float>*, idg::numSlots> probParams {};
    std::atomic<float>* noRepeatParam = nullptr;
    std::atomic<float>* keySourceParam = nullptr;
    std::atomic<float>* rootNoteParam = nullptr;
    std::atomic<float>* polyphonyParam = nullptr;
    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* oneShotParam = nullptr;
    std::atomic<float>* fadeParam = nullptr;
    std::atomic<float>* normalizeParam = nullptr;

    // File de pre-ecoute (interface -> audio), sans allocation
    struct PreviewRequest { int slot, slice; };
    juce::AbstractFifo previewFifo { 32 };
    std::array<PreviewRequest, 32> previewQueue {};

    std::array<std::atomic<float>, 2> outputLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSlicerProcessor)
};
