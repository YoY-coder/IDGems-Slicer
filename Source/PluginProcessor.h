#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine/SampleManager.h"
#include "Engine/VoiceEngine.h"
#include "Engine/Router.h"
#include "Engine/Sequencer.h"
#include "Engine/MapStore.h"
#include "Engine/FxChain.h"
#include "Engine/Master.h"
#include "Engine/RandomManager.h"

//==============================================================================
// IDGems Slicer - moteur audio.
//
//  - 4 emplacements de samples decoupes + aiguillage par probabilite
//  - Modes de jeu : Clavier / Scan / Accord
//  - Sequenceur polyrythmique synchro tempo (lance tant qu'une note est tenue)
//
// Regles audio : aucune allocation memoire / aucun acces disque dans
// processBlock(). Tout ce qui est lourd est prepare ailleurs puis echange
// sans blocage.
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

    static constexpr int baseWidth  = 1280;
    static constexpr int baseHeight = 800;
    juce::Point<int> getSavedEditorSize() const;
    void setSavedEditorSize (int w, int h);

    float getOutputLevel (int channel) const noexcept { return outputLevel[(size_t) juce::jlimit (0, 1, channel)].load(); }

    std::atomic<int> midiActivityCounter { 0 };

    idg::SampleManager& getSampleManager() noexcept { return sampleManager; }
    const idg::VoiceEngine& getVoiceEngine() const noexcept { return engine; }
    idg::PatternStore& getPatternStore() noexcept { return patternStore; }
    idg::MapStore& getMapStore() noexcept { return mapStore; }

    // Ordre de la chaine FX (glisser-deposer dans l'interface)
    std::array<int, idg::numFxModules> getFxOrder() const { return idg::FxChain::decodeOrder (fxChain.order.load()); }
    void setFxOrder (const std::array<int, idg::numFxModules>&);
    const idg::FxChain& getFxChain() const noexcept { return fxChain; }
    idg::RandomManager& getRandomManager() noexcept { return *randomManager; }

    // Position effective du curseur sur la carte (pour l'affichage)
    std::atomic<float> cursorX { 0.5f }, cursorY { 0.5f };

    void requestPreview (int slot, int slice);
    std::array<float, idg::numSlots> getProbabilities() const noexcept;

    // Pour l'affichage
    std::atomic<int> lastTriggerSlot { -1 }, lastTriggerSlice { -1 }, triggerCounter { 0 };
    std::atomic<juce::int64> currentSeqStep { -1 };   // -1 = sequenceur arrete
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // Etat d'interface sauvegarde avec le projet
    int getSelectedSlot() const;
    void setSelectedSlot (int);
    int getUiProperty (const juce::Identifier&, int defaultValue) const;
    void setUiProperty (const juce::Identifier&, int value);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //== Declenchements ========================================================
    struct Trigger
    {
        int slot = -1, slice = 0, note = -1;
        float velocity = 1.0f, semitones = 0.0f, startFraction = 0.0f;
        bool reverse = false, oneShot = false, freeze = false;
        int gateSamples = -1;
        float fxSend = -1.0f;   // < 0 : reglage global
    };

    struct Pending { juce::int64 time = 0; Trigger trigger; };

    struct TriggerRequest
    {
        int note = 60;
        float velocity = 1.0f;
        int forcedSlot = -1;           // -1 = aiguillage
        float scanOverride = -1.0f;    // >= 0 : position imposee (lane Slice)
        float extraSemitones = 0.0f;
        float startFraction = 0.0f;
        bool reverse = false;
        bool oneShot = false;
        bool freeze = false;           // lane Freeze du sequenceur
        float fxSend = -1.0f;          // lane Envoi FX (< 0 : reglage global)
        int gateSamples = -1;
        int voiceNote = -1;            // note utilisee pour le relachement
    };

    void handleMidi (const juce::MidiMessage&, int offsetInBlock);
    void buildTriggers (const TriggerRequest&, juce::int64 time, int repeats, double repeatSpacing);
    void schedule (juce::int64 time, const Trigger&) noexcept;
    void fire (const Trigger&) noexcept;
    void processSegment (juce::AudioBuffer<float>&, int start, int end);
    void fireSequencerStep (juce::int64 step, juce::int64 time);
    double stepBeat (juce::int64 step) const noexcept;
    double beatAt (int offsetInBlock) const noexcept { return blockStartBeat + offsetInBlock * beatsPerSample; }
    void startSequencer (int offsetInBlock);
    juce::Point<float> computeMapCursor (const TriggerRequest&, double beat) noexcept;
    float pathPhase (double beat) noexcept;
    int pickMapSlice (const idg::SampleData&, juce::Point<float> cursor) noexcept;
    void stopSequencer();

    //== Etat ==================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<double> hostBpm { 120.0 };
    idg::SampleManager sampleManager { [this] { return hostBpm.load(); } };
    idg::PatternStore patternStore;
    idg::MapStore mapStore;
    idg::MapPath mapPath;          // copie audio du trajet
    idg::FeatureTable spreadTable; // copie audio des caracteristiques etalees
    int spreadVersion = 0;
    int mapPathVersion = 0;
    idg::VoiceEngine engine;
    idg::Router router;
    std::array<idg::SampleData::Ptr, idg::numSlots> audioSlots;   // cache du thread audio

    idg::Pattern pattern;          // copie audio du sequenceur
    int patternVersion = 0;

    double currentSampleRate = 44100.0;
    juce::int64 sampleCounter = 0;          // horloge absolue (debut du bloc courant)
    double blockStartBeat = 0.0, beatsPerSample = 0.0, freeBeat = 0.0;
    bool hostPlaying = false;

    // Notes tenues (priorite a la derniere)
    std::array<int, 16> heldNotes {};
    std::array<float, 16> heldVelocities {};
    int numHeld = 0;
    float modWheel = 0.0f;

    // Sequenceur
    bool seqRunning = false;
    double seqOrigin = 0.0, seqStepBeats = 0.25;
    juce::int64 nextStep = 0;

    std::array<Pending, 256> pending {};
    int numPending = 0;

    juce::Random audioRandom;

    // Parametres
    std::atomic<float>* outputGainDb = nullptr;
    juce::SmoothedValue<float> outputGain;
    std::array<std::atomic<float>*, idg::numSlots> probParams {};
    std::atomic<float> *noRepeatParam = nullptr, *keySourceParam = nullptr, *rootNoteParam = nullptr,
                       *polyphonyParam = nullptr, *transposeParam = nullptr, *oneShotParam = nullptr,
                       *fadeParam = nullptr, *normalizeParam = nullptr,
                       *playModeParam = nullptr, *sliceSelectParam = nullptr, *scanRandomParam = nullptr,
                       *velToScanParam = nullptr, *wheelToScanParam = nullptr, *pitchFollowParam = nullptr,
                       *chordCountParam = nullptr, *chordSpreadParam = nullptr, *chordStepParam = nullptr,
                       *seqOnParam = nullptr, *seqRateParam = nullptr, *swingParam = nullptr, *grooveParam = nullptr,
                       *selectModeParam = nullptr, *mapXParam = nullptr, *mapYParam = nullptr, *mapRadiusParam = nullptr,
                       *mapAxisXParam = nullptr, *mapAxisYParam = nullptr, *pathOnParam = nullptr,
                       *pathLengthParam = nullptr, *pathModeParam = nullptr, *mapSpreadParam = nullptr,
                       *attackParam = nullptr, *decayParam = nullptr, *sustainParam = nullptr, *releaseParam = nullptr,
                       *clickAmountParam = nullptr, *clickTimeParam = nullptr,
                       *playStartParam = nullptr, *playLengthParam = nullptr, *reverseParam = nullptr,
                       *filterTypeParam = nullptr, *cutoffParam = nullptr, *resonanceParam = nullptr, *driveParam = nullptr,
                       *freezeOnParam = nullptr, *freezePosParam = nullptr, *freezeSizeParam = nullptr, *freezeFadeParam = nullptr;
    idg::VoiceEngine::LiveParams liveParams;

    std::unique_ptr<idg::RandomManager> randomManager;
    std::atomic<float> *mutateOnParam = nullptr, *mutateBarsParam = nullptr;
    juce::int64 lastMutateBar = -1;

    idg::FxChain fxChain;
    idg::Master master;
    juce::AudioBuffer<float> fxBus;
    idg::FxParams fxParams;
    idg::MasterParams masterParams;
    void readFxParams() noexcept;

    std::atomic<float> *fxSendParam = nullptr;
    std::array<std::atomic<float>*, idg::numFxModules> fxOnParams {}, fxMixParams {};
    std::atomic<float> *crushBitsParam = nullptr, *crushDownParam = nullptr,
                       *fxFilterTypeParam = nullptr, *fxCutoffParam = nullptr, *fxResoParam = nullptr,
                       *tapeStopParam = nullptr, *tapeTimeParam = nullptr,
                       *stutterParam = nullptr, *stutterDivParam = nullptr, *stutterChanceParam = nullptr,
                       *grainDensityParam = nullptr, *grainSizeParam = nullptr, *grainSprayParam = nullptr, *grainPitchParam = nullptr,
                       *ottDepthParam = nullptr, *ottTimeParam = nullptr, *ottUpParam = nullptr, *ottDownParam = nullptr,
                       *clipOnParam = nullptr, *clipDriveParam = nullptr, *masterMixParam = nullptr,
                       *limiterOnParam = nullptr, *ceilingParam = nullptr;

    // Pre-ecoute (interface -> audio), sans allocation
    struct PreviewRequest { int slot, slice; };
    juce::AbstractFifo previewFifo { 32 };
    std::array<PreviewRequest, 32> previewQueue {};

    std::array<std::atomic<float>, 2> outputLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IDGemsSlicerProcessor)
};
