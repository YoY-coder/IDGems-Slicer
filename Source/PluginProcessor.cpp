#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/SliceAnalysis.h"

namespace
{
    namespace IDs
    {
        const juce::Identifier editorWidth  { "editorWidth" };
        const juce::Identifier editorHeight { "editorHeight" };
        const juce::Identifier stateVersion { "stateVersion" };
        const juce::Identifier selectedSlot { "selectedSlot" };
        const juce::Identifier slots        { "Slots" };
        const juce::Identifier sequencer    { "Sequencer" };
        const juce::Identifier mapPath      { "MapPath" };
        const juce::Identifier fxOrder      { "fxOrder" };
        const juce::Identifier random       { "Random" };
        const juce::Identifier exportFolder { "exportFolder" };
        const juce::Identifier presetName   { "presetName" };
    }

    constexpr int currentStateVersion = 4;
    constexpr int previewNote = -2;
    constexpr int sequencerNote = -3;
    constexpr int maxChord = 8;

    enum PlayMode { modeKeyboard = 0, modeScan, modeChord };
}

//==============================================================================
IDGemsSlicerProcessor::IDGemsSlicerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSlicerState", createParameterLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    outputGainDb = p ("outputGain");
    for (int i = 0; i < idg::numSlots; ++i)
        probParams[(size_t) i] = apvts.getRawParameterValue ("prob" + juce::String (i + 1));
    noRepeatParam    = p ("noRepeat");
    keySourceParam   = p ("keySource");
    rootNoteParam    = p ("rootNote");
    polyphonyParam   = p ("polyphony");
    transposeParam   = p ("transpose");
    oneShotParam     = p ("oneShot");
    fadeParam        = p ("fadeMs");
    normalizeParam   = p ("normalize");
    playModeParam    = p ("playMode");
    sliceSelectParam = p ("sliceSelect");
    scanRandomParam  = p ("scanRandom");
    velToScanParam   = p ("velToScan");
    wheelToScanParam = p ("wheelToScan");
    pitchFollowParam = p ("pitchFollow");
    chordCountParam  = p ("chordCount");
    chordSpreadParam = p ("chordSpread");
    chordStepParam   = p ("chordStep");
    seqOnParam       = p ("seqOn");
    seqRateParam     = p ("seqRate");
    swingParam       = p ("swing");
    grooveParam      = p ("groove");
    selectModeParam  = p ("selectMode");
    mapXParam        = p ("mapX");
    mapYParam        = p ("mapY");
    mapRadiusParam   = p ("mapRadius");
    mapAxisXParam    = p ("mapAxisX");
    mapAxisYParam    = p ("mapAxisY");
    pathOnParam      = p ("pathOn");
    pathLengthParam  = p ("pathLength");
    pathModeParam    = p ("pathMode");
    mapSpreadParam   = p ("mapSpread");
    attackParam      = p ("attack");
    decayParam       = p ("decay");
    sustainParam     = p ("sustain");
    releaseParam     = p ("release");
    clickAmountParam = p ("clickAmount");
    clickTimeParam   = p ("clickTime");
    playStartParam   = p ("playStart");
    playLengthParam  = p ("playLength");
    reverseParam     = p ("reverse");
    filterTypeParam  = p ("filterType");
    cutoffParam      = p ("cutoff");
    resonanceParam   = p ("resonance");
    driveParam       = p ("drive");
    freezeOnParam    = p ("freezeOn");
    freezePosParam   = p ("freezePos");
    freezeSizeParam  = p ("freezeSize");
    freezeFadeParam  = p ("freezeFade");

    fxSendParam = p ("fxSend");
    static const char* fxIds[] { "crush", "fxFilter", "tape", "stutter", "grain" };
    for (int i = 0; i < idg::numFxModules; ++i)
    {
        fxOnParams[(size_t) i]  = apvts.getRawParameterValue (juce::String (fxIds[i]) + "On");
        fxMixParams[(size_t) i] = apvts.getRawParameterValue (juce::String (fxIds[i]) + "Mix");
    }
    crushBitsParam     = p ("crushBits");
    crushDownParam     = p ("crushDown");
    fxFilterTypeParam  = p ("fxFilterType");
    fxCutoffParam      = p ("fxCutoff");
    fxResoParam        = p ("fxReso");
    tapeStopParam      = p ("tapeStop");
    tapeTimeParam      = p ("tapeTime");
    stutterParam       = p ("stutterHold");
    stutterDivParam    = p ("stutterDiv");
    stutterChanceParam = p ("stutterChance");
    grainDensityParam  = p ("grainDensity");
    grainSizeParam     = p ("grainSize");
    grainSprayParam    = p ("grainSpray");
    grainPitchParam    = p ("grainPitch");
    ottDepthParam      = p ("ottDepth");
    ottTimeParam       = p ("ottTime");
    ottUpParam         = p ("ottUp");
    ottDownParam       = p ("ottDown");
    clipOnParam        = p ("clipOn");
    clipDriveParam     = p ("clipDrive");
    masterMixParam     = p ("masterMix");
    limiterOnParam     = p ("limiterOn");
    ceilingParam       = p ("ceiling");
    apvts.state.setProperty (IDs::fxOrder, fxChain.order.load(), nullptr);

    mutateOnParam   = p ("mutateOn");
    mutateBarsParam = p ("mutateBars");
    randomManager = std::make_unique<idg::RandomManager> (apvts, patternStore, sampleManager);
    undoHistory = std::make_unique<idg::UndoHistory> (*this, patternStore, mapStore, sampleManager);

    apvts.state.setProperty (IDs::editorWidth,  baseWidth,  nullptr);
    apvts.state.setProperty (IDs::editorHeight, baseHeight, nullptr);
    apvts.state.setProperty (IDs::selectedSlot, 0, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout IDGemsSlicerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outputGain", 1 }, "Output Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Aiguillage
    for (int i = 1; i <= idg::numSlots; ++i)
        layout.add (percent ("prob" + String (i), "Probabilite Sample " + String (i), 0.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "noRepeat", 1 }, "Pas de repetition", false));

    // Jeu
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "keySource", 1 }, "Source Clavier",
        StringArray { "Auto", "Sample 1", "Sample 2", "Sample 3", "Sample 4" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "rootNote", 1 }, "Note racine", 0, 127, 36));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "polyphony", 1 }, "Polyphonie", 1, 16, 8));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "transpose", 1 }, "Transposition",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "oneShot", 1 }, "One-shot", false));

    // Decoupe (global)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fadeMs", 1 }, "Anti-clic",
        NormalisableRange<float> (0.0f, 50.0f, 0.1f, 0.5f), 3.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "normalize", 1 }, "Normaliser les slices", false));

    // Modes de jeu (etape 4)
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "playMode", 1 }, "Mode de jeu", StringArray { "Clavier", "Scan", "Accord" }, 0));
    layout.add (percent ("sliceSelect", "Slice Select", 0.0f, 100.0f, 0.0f));
    layout.add (percent ("scanRandom", "Scan Random", 0.0f, 100.0f, 0.0f));
    layout.add (percent ("velToScan", "Velocite > Scan", -100.0f, 100.0f, 0.0f));
    layout.add (percent ("wheelToScan", "Molette > Scan", -100.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pitchFollow", 1 }, "Pitch suit la note", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "chordCount", 1 }, "Accord : slices", 2, maxChord, 3));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "chordSpread", 1 }, "Accord : spread",
        NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.5f), 60.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "chordStep", 1 }, "Accord : ecart", 1, 16, 1));

    // Sequenceur
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "seqOn", 1 }, "Sequenceur", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqRate", 1 }, "Sequenceur : resolution",
                                                        idg::seqRateNames(), 2));
    layout.add (percent ("swing", "Swing", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "groove", 1 }, "Groove", idg::grooveNames(), 0));

    // Carte des slices (etape 5)
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "selectMode", 1 }, "Slice Select : source",
                                                        StringArray { "Lineaire", "Carte" }, 1));
    layout.add (percent ("mapX", "Carte X", 0.0f, 100.0f, 50.0f));
    layout.add (percent ("mapY", "Carte Y", 0.0f, 100.0f, 50.0f));
    layout.add (percent ("mapRadius", "Carte : rayon", 0.0f, 50.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mapAxisX", 1 }, "Carte : axe X", idg::featureNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mapAxisY", 1 }, "Carte : axe Y", idg::featureNames(), 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pathOn", 1 }, "Trajet", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pathLength", 1 }, "Trajet : duree",
                                                        idg::pathLengthNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pathMode", 1 }, "Trajet : mode",
                                                        idg::pathModeNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mapSpread", 1 }, "Carte : etaler", true));

    // Voix (etape 6)
    auto ms = [] (const String& id, const String& name, float lo, float hi, float def, float skew)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.1f, skew), def,
                                                      AudioParameterFloatAttributes().withLabel ("ms"));
    };
    layout.add (ms ("attack", "Attaque", 0.0f, 2000.0f, 1.0f, 0.3f));
    layout.add (ms ("decay", "Decroissance", 1.0f, 3000.0f, 200.0f, 0.3f));
    layout.add (percent ("sustain", "Maintien", 0.0f, 100.0f, 100.0f));
    layout.add (ms ("release", "Relachement", 1.0f, 5000.0f, 30.0f, 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "clickAmount", 1 }, "Pitch clic : quantite",
                                                       NormalisableRange<float> (0.0f, 48.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (ms ("clickTime", "Pitch clic : duree", 1.0f, 300.0f, 25.0f, 0.5f));
    layout.add (percent ("playStart", "Start", 0.0f, 95.0f, 0.0f));
    layout.add (percent ("playLength", "Longueur", 2.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "filterType", 1 }, "Filtre : type",
                                                        StringArray { "Off", "Passe-bas", "Passe-haut", "Passe-bande" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Filtre : frequence",
                                                       NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 20000.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "resonance", 1 }, "Filtre : resonance",
                                                       NormalisableRange<float> (0.5f, 10.0f, 0.01f, 0.4f), 0.707f));
    layout.add (percent ("drive", "Drive", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freezeOn", 1 }, "Slice Freeze", false));
    layout.add (percent ("freezePos", "Freeze : position", 0.0f, 100.0f, 30.0f));
    layout.add (ms ("freezeSize", "Freeze : taille du grain", 5.0f, 1000.0f, 80.0f, 0.4f));
    layout.add (percent ("freezeFade", "Freeze : fondu", 0.0f, 100.0f, 40.0f));

    // Chaine FX IDGems (etape 7)
    layout.add (percent ("fxSend", "Envoi FX", 0.0f, 100.0f, 100.0f));
    const char* fxIds[] { "crush", "fxFilter", "tape", "stutter", "grain" };
    for (int i = 0; i < idg::numFxModules; ++i)
    {
        const auto name = idg::fxModuleNames()[i];
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { String (fxIds[i]) + "On", 1 }, name + " : on", false));
        layout.add (percent (String (fxIds[i]) + "Mix", name + " : dry/wet", 0.0f, 100.0f, 100.0f));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushBits", 1 }, "Bitcrush : bits",
                                                       NormalisableRange<float> (1.0f, 16.0f, 0.1f), 6.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushDown", 1 }, "Bitcrush : downsample",
                                                       NormalisableRange<float> (1.0f, 40.0f, 0.1f, 0.5f), 4.0f,
                                                       AudioParameterFloatAttributes().withLabel ("x")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxFilterType", 1 }, "Filtre FX : type",
                                                        StringArray { "Passe-bas", "Passe-haut", "Passe-bande" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxCutoff", 1 }, "Filtre FX : frequence",
                                                       NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 1200.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxReso", 1 }, "Filtre FX : resonance",
                                                       NormalisableRange<float> (0.5f, 12.0f, 0.01f, 0.4f), 2.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tapeStop", 1 }, "Tape stop : declencher", false));
    layout.add (ms ("tapeTime", "Tape stop : duree", 50.0f, 3000.0f, 600.0f, 0.5f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stutterHold", 1 }, "Stutter : declencher", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stutterDiv", 1 }, "Stutter : longueur",
                                                        StringArray { "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T" }, 2));
    layout.add (percent ("stutterChance", "Stutter : chance auto", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grainDensity", 1 }, "Granulaire : densite",
                                                       NormalisableRange<float> (1.0f, 60.0f, 0.1f, 0.5f), 15.0f,
                                                       AudioParameterFloatAttributes().withLabel ("/s")));
    layout.add (ms ("grainSize", "Granulaire : taille", 10.0f, 500.0f, 120.0f, 0.5f));
    layout.add (ms ("grainSpray", "Granulaire : dispersion", 0.0f, 2000.0f, 400.0f, 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grainPitch", 1 }, "Granulaire : pitch random",
                                                       NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("st")));

    // Master
    layout.add (percent ("ottDepth", "OTT : profondeur", 0.0f, 100.0f, 0.0f));
    layout.add (percent ("ottTime", "OTT : temps", 0.0f, 100.0f, 50.0f));
    layout.add (percent ("ottUp", "OTT : vers le haut", 0.0f, 100.0f, 100.0f));
    layout.add (percent ("ottDown", "OTT : vers le bas", 0.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "clipOn", 1 }, "Soft clip", false));
    layout.add (percent ("clipDrive", "Soft clip : drive", 0.0f, 100.0f, 20.0f));
    layout.add (percent ("masterMix", "Master : dry/wet", 0.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "limiterOn", 1 }, "Limiteur", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Limiteur : plafond",
                                                       NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -0.3f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    // Random (etape 8)
    layout.add (percent ("randomAmount", "Random : dosage", 0.0f, 100.0f, 60.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "randomMode", 1 }, "Random : mode",
                                                        StringArray { "Tout", "Garder le groove", "Garder les sons" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mutateOn", 1 }, "Mutate auto", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "mutateBars", 1 }, "Mutate : toutes les N mesures", 1, 32, 4));
    layout.add (percent ("mutateRate", "Mutate : taux", 0.0f, 100.0f, 15.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "morphOn", 1 }, "Morph pad", false));
    layout.add (percent ("morphX", "Morph X", 0.0f, 100.0f, 50.0f));
    layout.add (percent ("morphY", "Morph Y", 0.0f, 100.0f, 50.0f));

    return layout;
}

//==============================================================================
void IDGemsSlicerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    const int maxBlock = juce::jmax (4096, samplesPerBlock);
    engine.prepare (currentSampleRate, maxBlock);
    fxChain.prepare (currentSampleRate, maxBlock);
    master.prepare (currentSampleRate, maxBlock);
    fxBus.setSize (2, maxBlock);
    outputGain.reset (currentSampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    numPending = 0;
    numHeld = 0;
    stopSequencer();
}

bool IDGemsSlicerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

std::array<float, idg::numSlots> IDGemsSlicerProcessor::getProbabilities() const noexcept
{
    std::array<float, idg::numSlots> p {};
    for (size_t i = 0; i < p.size(); ++i) p[i] = probParams[i]->load() * 0.01f;
    return p;
}

void IDGemsSlicerProcessor::requestPreview (int slot, int slice)
{
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0) previewQueue[(size_t) scope.startIndex1] = { slot, slice };
    else if (scope.blockSize2 > 0) previewQueue[(size_t) scope.startIndex2] = { slot, slice };
}

//==============================================================================
// Declenchements
//==============================================================================
void IDGemsSlicerProcessor::schedule (juce::int64 time, const Trigger& t) noexcept
{
    if (numPending < (int) pending.size())
        pending[(size_t) numPending++] = { time, t };
}

void IDGemsSlicerProcessor::fire (const Trigger& t) noexcept
{
    const auto& data = audioSlots[(size_t) t.slot];
    if (data == nullptr || data->slices.empty()) return;

    idg::VoiceEngine::NoteParams p;
    p.slot = t.slot;
    p.slice = juce::jlimit (0, data->numSlices() - 1, t.slice);
    p.note = t.note;
    p.velocity = t.velocity;
    p.pitchRatio = data->audio->sampleRate / currentSampleRate * std::pow (2.0, (double) t.semitones / 12.0);
    p.oneShot = t.oneShot;
    p.polyphony = (int) polyphonyParam->load();
    p.fadeSeconds = fadeParam->load() * 0.001;
    p.normalise = normalizeParam->load() > 0.5f;
    p.reverse = t.reverse != (reverseParam->load() > 0.5f);   // reverse global inverse le sens
    p.startFraction = juce::jmin (0.95f, t.startFraction + playStartParam->load() * 0.01f);
    p.lengthFraction = playLengthParam->load() * 0.01f;
    p.gateSamples = t.gateSamples;
    p.adsr = { attackParam->load() * 0.001f, decayParam->load() * 0.001f,
               sustainParam->load() * 0.01f, releaseParam->load() * 0.001f };
    p.clickSemitones = clickAmountParam->load();
    p.clickSeconds = clickTimeParam->load() * 0.001f;
    p.freeze = t.freeze || freezeOnParam->load() > 0.5f;
    p.fxSend = t.fxSend >= 0.0f ? t.fxSend : fxSendParam->load() * 0.01f;
    engine.noteOn (data, p);

    if (logTriggers && triggerLog.size() < triggerLog.capacity())   // pas d'allocation : capacite reservee
    {
        const auto& sl = data->slices[(size_t) p.slice];
        int len = t.gateSamples > 0 ? t.gateSamples
                                    : (int) ((sl.end - sl.start) * p.lengthFraction / juce::jmax (0.01, p.pitchRatio));
        triggerLog.push_back ({ currentFireTime, t.slot, p.slice, t.velocity, juce::jmax (1, len) });
    }

    lastTriggerSlot.store (t.slot);
    lastTriggerSlice.store (p.slice);
    triggerCounter.fetch_add (1);
}

// Un declenchement = UN sample (choisi par l'aiguillage ou force), puis une
// ou plusieurs slices de ce sample (Mode Accord), eventuellement repetees (ratchet).
void IDGemsSlicerProcessor::buildTriggers (const TriggerRequest& r, juce::int64 time, int repeats, double repeatSpacing)
{
    std::array<bool, idg::numSlots> loaded {};
    for (size_t i = 0; i < loaded.size(); ++i)
        loaded[i] = audioSlots[i] != nullptr && ! audioSlots[i]->slices.empty();

    int slot = -1;
    if (juce::isPositiveAndBelow (r.forcedSlot, idg::numSlots) && loaded[(size_t) r.forcedSlot])
        slot = r.forcedSlot;
    else
        slot = router.pick (loaded, getProbabilities(), noRepeatParam->load() > 0.5f);
    if (slot < 0) return;

    const int n = audioSlots[(size_t) slot]->numSlices();
    const int mode = (int) playModeParam->load();
    const int root = (int) rootNoteParam->load();

    int base = 0;
    if (mode == modeKeyboard)
    {
        base = (((r.note - root) % n) + n) % n;
        if (r.scanOverride >= 0.0f)
            base = (base + juce::roundToInt (r.scanOverride * (float) (n - 1))) % n;
    }
    else if (selectModeParam->load() > 0.5f)
    {
        // Carte : slice la plus proche du curseur, dans le sample choisi par l'aiguillage
        const double beat = blockStartBeat + (double) (time - sampleCounter) * beatsPerSample;
        base = pickMapSlice (*audioSlots[(size_t) slot], computeMapCursor (r, beat));
    }
    else
    {
        float pos = r.scanOverride >= 0.0f ? r.scanOverride : sliceSelectParam->load() * 0.01f;
        pos += wheelToScanParam->load() * 0.01f * modWheel;
        pos += velToScanParam->load() * 0.01f * r.velocity;
        pos += (audioRandom.nextFloat() - 0.5f) * scanRandomParam->load() * 0.01f;
        if (pos < 0.0f || pos > 1.0f)
            pos -= std::floor (pos);   // reboucle dans 0..1 (100 % reste la derniere slice)
        base = juce::jlimit (0, n - 1, juce::roundToInt (pos * (float) (n - 1)));
    }

    Trigger t;
    t.slot = slot;
    t.note = r.voiceNote;
    t.velocity = r.velocity;
    t.semitones = transposeParam->load() + r.extraSemitones
                + ((mode != modeKeyboard && pitchFollowParam->load() > 0.5f) ? (float) (r.note - root) : 0.0f);
    t.startFraction = r.startFraction;
    t.reverse = r.reverse;
    t.oneShot = r.oneShot;
    t.freeze = r.freeze;
    t.fxSend = r.fxSend;
    t.gateSamples = r.gateSamples;

    const int count = mode == modeChord ? juce::jlimit (2, maxChord, (int) chordCountParam->load()) : 1;
    const int stepBetween = (int) chordStepParam->load();
    const double spread = chordSpreadParam->load() * 0.001 * currentSampleRate;

    for (int rep = 0; rep < juce::jmax (1, repeats); ++rep)
        for (int k = 0; k < count; ++k)
        {
            t.slice = (base + k * stepBetween) % n;
            schedule (time + (juce::int64) (rep * repeatSpacing + k * spread), t);
        }
}

//==============================================================================
// Carte des slices
//==============================================================================
float IDGemsSlicerProcessor::pathPhase (double beat) noexcept
{
    const double len = idg::pathLengthInBeats ((int) pathLengthParam->load());
    const float u = (float) (beat / len - std::floor (beat / len));
    switch ((int) pathModeParam->load())
    {
        case 1:  return u < 0.5f ? u * 2.0f : 2.0f - u * 2.0f;   // ping-pong
        case 2:  return audioRandom.nextFloat();                  // random
        default: return u;                                        // aller simple
    }
}

juce::Point<float> IDGemsSlicerProcessor::computeMapCursor (const TriggerRequest& r, double beat) noexcept
{
    juce::Point<float> c { mapXParam->load() * 0.01f, mapYParam->load() * 0.01f };
    const bool path = pathOnParam->load() > 0.5f && mapPath.count >= 2;

    if (path)
        c = mapPath.at (r.scanOverride >= 0.0f ? r.scanOverride : pathPhase (beat));
    else if (r.scanOverride >= 0.0f)
        c.x = r.scanOverride;   // lane Slice : deplacement horizontal

    c.x += wheelToScanParam->load() * 0.01f * modWheel + velToScanParam->load() * 0.01f * r.velocity;
    const float rnd = scanRandomParam->load() * 0.01f;
    c.x += (audioRandom.nextFloat() - 0.5f) * rnd;
    c.y += (audioRandom.nextFloat() - 0.5f) * rnd;
    c = { juce::jlimit (0.0f, 1.0f, c.x), juce::jlimit (0.0f, 1.0f, c.y) };

    cursorX.store (c.x);
    cursorY.store (c.y);
    return c;
}

int IDGemsSlicerProcessor::pickMapSlice (const idg::SampleData& d, juce::Point<float> c) noexcept
{
    const int ax = juce::jlimit (0, idg::numFeatures - 1, (int) mapAxisXParam->load());
    const int ay = juce::jlimit (0, idg::numFeatures - 1, (int) mapAxisYParam->load());
    const float radius = mapRadiusParam->load() * 0.01f;

    int nearest = 0, numCandidates = 0;
    float best = 1.0e9f;
    std::array<int, idg::maxSlices> candidates;   // tableau fixe : pas d'allocation

    // Caracteristiques etalees (si la table correspond bien a ce sample)
    const std::array<std::array<float, idg::numFeatures>, idg::maxSlices>* spread = nullptr;
    if (mapSpreadParam->load() > 0.5f)
        for (int s = 0; s < idg::numSlots; ++s)
            if (spreadTable.owners[(size_t) s] == &d && spreadTable.counts[(size_t) s] == d.numSlices())
                spread = &spreadTable.values[(size_t) s];

    for (int i = 0; i < d.numSlices(); ++i)
    {
        const auto& f = spread != nullptr ? (*spread)[(size_t) i] : d.slices[(size_t) i].features;
        const float dx = f[(size_t) ax] - c.x, dy = f[(size_t) ay] - c.y;
        const float dist = std::sqrt (dx * dx + dy * dy);
        if (dist < best) { best = dist; nearest = i; }
        if (radius > 0.0f && dist <= radius && numCandidates < (int) candidates.size())
            candidates[(size_t) numCandidates++] = i;
    }

    // Grand rayon = plus aleatoire : tirage parmi les slices dans le cercle
    if (numCandidates > 1)
        return candidates[(size_t) audioRandom.nextInt (numCandidates)];
    return nearest;
}

//==============================================================================
// Sequenceur
//==============================================================================
double IDGemsSlicerProcessor::stepBeat (juce::int64 step) const noexcept
{
    const double swing = (step & 1) ? swingParam->load() * 0.01 * 0.5 : 0.0;
    const double groove = idg::grooveTiming ((int) grooveParam->load(), step);
    return seqOrigin + ((double) step + swing + groove) * seqStepBeats;
}

void IDGemsSlicerProcessor::startSequencer (int offsetInBlock)
{
    seqRunning = true;
    seqStepBeats = idg::seqRateInBeats ((int) seqRateParam->load());
    const double now = beatAt (offsetInBlock);

    if (hostPlaying)
    {
        // Cale sur la grille d'Ableton ; une note jouee juste apres un pas (30 ms) le declenche quand meme
        seqOrigin = 0.0;
        const double tolerance = 0.03 * hostBpm.load() / 60.0;
        nextStep = (juce::int64) std::ceil ((now - tolerance) / seqStepBeats);
        while (stepBeat (nextStep) < now - tolerance) ++nextStep;
    }
    else
    {
        seqOrigin = now;
        nextStep = 0;
    }
}

void IDGemsSlicerProcessor::stopSequencer()
{
    seqRunning = false;
    currentSeqStep.store (-1);
    // On annule les repetitions / accords du sequenceur encore en attente
    int j = 0;
    for (int i = 0; i < numPending; ++i)
        if (pending[(size_t) i].trigger.note != sequencerNote)
            pending[(size_t) j++] = pending[(size_t) i];
    numPending = j;
}

void IDGemsSlicerProcessor::fireSequencerStep (juce::int64 step, juce::int64 time)
{
    currentSeqStep.store (step);
    if (numHeld == 0) return;

    if (audioRandom.nextFloat() >= pattern.valueAt (idg::laneProbability, step))
        return;

    const double stepSamples = seqStepBeats / juce::jmax (1.0e-9, beatsPerSample);
    const int ratchets = juce::jlimit (1, 8, (int) pattern.valueAt (idg::laneRatchet, step));
    const double spacing = stepSamples / ratchets;

    TriggerRequest r;
    r.note = heldNotes[(size_t) numHeld - 1];
    r.velocity = heldVelocities[(size_t) numHeld - 1]
               * pattern.valueAt (idg::laneVelocity, step)
               * idg::grooveVelocity ((int) grooveParam->load(), step);
    r.forcedSlot = (int) pattern.valueAt (idg::laneSample, step) - 1;
    r.scanOverride = pattern.lanes[idg::laneSlice].enabled ? pattern.valueAt (idg::laneSlice, step) : -1.0f;
    r.extraSemitones = pattern.valueAt (idg::lanePitch, step);
    r.startFraction = pattern.valueAt (idg::laneStart, step);
    r.reverse = pattern.valueAt (idg::laneReverse, step) > 0.5f;
    r.freeze = pattern.valueAt (idg::laneFreeze, step) > 0.5f;
    r.fxSend = pattern.lanes[idg::laneFxSend].enabled ? pattern.valueAt (idg::laneFxSend, step) : -1.0f;
    r.oneShot = oneShotParam->load() > 0.5f;
    r.gateSamples = r.oneShot ? -1 : juce::jmax (64, (int) (pattern.valueAt (idg::laneGate, step) * spacing));
    r.voiceNote = sequencerNote;

    if (r.velocity > 0.0f)
        buildTriggers (r, time, ratchets, spacing);
}

void IDGemsSlicerProcessor::processSegment (juce::AudioBuffer<float>& buffer, int start, int end)
{
    if (end <= start) return;

    // Pas du sequenceur qui tombent dans ce segment
    if (seqRunning)
    {
        const double segStart = beatAt (start), segEnd = beatAt (end);
        seqStepBeats = idg::seqRateInBeats ((int) seqRateParam->load());

        // Saut de position de l'hote (boucle, deplacement) : on se recale
        if (stepBeat (nextStep) < segStart - seqStepBeats || stepBeat (nextStep) > segEnd + 4.0 * seqStepBeats)
        {
            nextStep = (juce::int64) std::ceil ((segStart - seqOrigin) / seqStepBeats);
            if (nextStep < 0) nextStep = 0;
        }

        while (stepBeat (nextStep) < segEnd)
        {
            const double b = juce::jmax (segStart, stepBeat (nextStep));
            const int offset = juce::jlimit (start, end - 1, start + (int) ((b - segStart) / juce::jmax (1.0e-12, beatsPerSample)));
            fireSequencerStep (nextStep, sampleCounter + offset);
            ++nextStep;
        }
    }

    // Rendu entre les declenchements en attente, dans l'ordre
    int cursor = start;
    const juce::int64 segEndTime = sampleCounter + end;
    for (;;)
    {
        int idx = -1;
        for (int i = 0; i < numPending; ++i)
            if (pending[(size_t) i].time < segEndTime && (idx < 0 || pending[(size_t) i].time < pending[(size_t) idx].time))
                idx = i;
        if (idx < 0) break;

        const int at = (int) juce::jlimit ((juce::int64) cursor, (juce::int64) end, pending[(size_t) idx].time - sampleCounter);
        if (at > cursor) { engine.render (buffer, fxBus, cursor, at - cursor, liveParams); cursor = at; }

        const auto t = pending[(size_t) idx].trigger;
        currentFireTime = sampleCounter + at;
        pending[(size_t) idx] = pending[(size_t) --numPending];
        fire (t);
    }
    if (cursor < end) engine.render (buffer, fxBus, cursor, end - cursor, liveParams);
}

//==============================================================================
// MIDI
//==============================================================================
void IDGemsSlicerProcessor::handleMidi (const juce::MidiMessage& m, int offset)
{
    const bool seqOn = seqOnParam->load() > 0.5f;

    // Banque de patterns : notes C-2 a G-2 (0 a 7) ou Program Change 1 a 8
    if ((m.isNoteOnOrOff() && m.getNoteNumber() < idg::numPatterns) || m.isProgramChange())
    {
        if (m.isNoteOn() || m.isProgramChange())
        {
            patternStore.selectPattern ((m.isProgramChange() ? m.getProgramChangeNumber() : m.getNoteNumber()) % idg::numPatterns);
            pattern = patternBank[(size_t) patternStore.getCurrentIndex()];
            patternIndexUsed = patternStore.getCurrentIndex();
        }
        return;
    }

    if (m.isNoteOn())
    {
        midiActivityCounter.fetch_add (1, std::memory_order_relaxed);
        const int note = m.getNoteNumber();

        // Pile des notes tenues (la derniere est prioritaire)
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                {
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
                }
                --numHeld;
                break;
            }
        if (numHeld == (int) heldNotes.size())
        {
            for (int j = 0; j < numHeld - 1; ++j)
            {
                heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
            }
            --numHeld;
        }
        heldNotes[(size_t) numHeld] = note;
        heldVelocities[(size_t) numHeld] = m.getFloatVelocity();
        ++numHeld;

        if (seqOn)
        {
            if (! seqRunning) startSequencer (offset);
            return;
        }

        // Jeu direct : un declenchement par note
        const int source = (int) keySourceParam->load();
        TriggerRequest r;
        r.note = note;
        r.velocity = m.getFloatVelocity();
        r.forcedSlot = source - 1;
        r.oneShot = oneShotParam->load() > 0.5f;
        r.voiceNote = note;
        buildTriggers (r, sampleCounter + offset, 1, 0.0);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                {
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
                }
                --numHeld;
                break;
            }

        engine.noteOff (note);
        if (numHeld == 0 && seqRunning)
            stopSequencer();
    }
    else if (m.isController() && m.getControllerNumber() == 1)
    {
        modWheel = (float) m.getControllerValue() / 127.0f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        numHeld = 0;
        numPending = 0;
        stopSequencer();
        engine.allNotesOff (m.isAllSoundOff());
    }
}

void IDGemsSlicerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    // Bloc plus grand que prevu (rare) : on decoupe pour ne jamais allouer
    if (numSamples > fxBus.getNumSamples() || buffer.getNumChannels() < 2)
    {
        if (buffer.getNumChannels() >= 2)
        {
            int done = 0;
            juce::MidiBuffer empty;
            while (done < numSamples)
            {
                const int n = juce::jmin (fxBus.getNumSamples(), numSamples - done);
                juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), 2, done, n);
                juce::MidiBuffer partMidi;
                if (done == 0) partMidi.swapWith (midi);   // le MIDI va dans le premier morceau
                processBlock (part, done == 0 ? partMidi : empty);
                done += n;
            }
            return;
        }
    }
    fxBus.clear (0, numSamples);

    sampleManager.updateAudioCache (audioSlots);

    // Reglages de voix modulables en temps reel (filtre, drive, freeze)
    liveParams.filterType = (int) filterTypeParam->load();
    liveParams.cutoff = cutoffParam->load();
    liveParams.resonance = resonanceParam->load();
    liveParams.drive = driveParam->load() * 0.01f;
    liveParams.freezePosition = freezePosParam->load() * 0.01f;
    liveParams.freezeSeconds = freezeSizeParam->load() * 0.001f;
    liveParams.freezeFade = freezeFadeParam->load() * 0.01f;
    {
        const int before = patternVersion;
        patternStore.pull (patternBank, patternVersion);
        const int idx = patternStore.getCurrentIndex();
        if (before != patternVersion || idx != patternIndexUsed)
        {
            pattern = patternBank[(size_t) idx];
            patternIndexUsed = idx;
        }
    }
    mapStore.pull (mapPath, mapPathVersion);
    sampleManager.pullSpreadTable (spreadTable, spreadVersion);

    // Tempo et position de l'hote
    bool playing = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);
            if (pos->getIsPlaying())
                if (auto p = pos->getPpqPosition()) { playing = true; ppq = *p; }
        }

    if (seqRunning && playing != hostPlaying)
    {
        // L'hote demarre / s'arrete pendant que le sequenceur tourne : on se recale
        hostPlaying = playing;
        blockStartBeat = playing ? ppq : freeBeat;
        startSequencer (0);
    }
    hostPlaying = playing;
    beatsPerSample = hostBpm.load() / 60.0 / currentSampleRate;
    blockStartBeat = playing ? ppq : freeBeat;

    // Affichage du curseur de la carte quand un trajet le fait bouger
    if (pathOnParam->load() > 0.5f && mapPath.count >= 2 && (int) pathModeParam->load() != 2)
    {
        const auto c = mapPath.at (pathPhase (blockStartBeat));
        cursorX.store (c.x);
        cursorY.store (c.y);
    }
    else if (pathOnParam->load() < 0.5f && numHeld == 0)
    {
        cursorX.store (mapXParam->load() * 0.01f);
        cursorY.store (mapYParam->load() * 0.01f);
    }

    // Pre-ecoutes demandees par l'interface
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto handle = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto r = previewQueue[(size_t) (start + i)];
                if (juce::isPositiveAndBelow (r.slot, idg::numSlots))
                {
                    Trigger t;
                    t.slot = r.slot;
                    t.slice = r.slice;
                    t.note = previewNote;
                    t.velocity = 0.9f;
                    t.oneShot = true;
                    t.semitones = transposeParam->load();
                    schedule (sampleCounter, t);
                }
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // MIDI precis a l'echantillon pres
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        processSegment (buffer, pos, at);
        pos = at;
        handleMidi (meta.getMessage(), at);
    }
    processSegment (buffer, pos, numSamples);

    // Le sequenceur peut etre coupe pendant qu'il tourne
    if (seqRunning && seqOnParam->load() < 0.5f)
        stopSequencer();
    else if (! seqRunning && numHeld > 0 && seqOnParam->load() > 0.5f)
        startSequencer (numSamples - 1);

    // Chaine FX (bus d'envoi) puis retour dans la sortie
    readFxParams();
    if (buffer.getNumChannels() >= 2)
    {
        fxChain.process (fxBus, numSamples, fxParams, { blockStartBeat, beatsPerSample });
        for (int ch = 0; ch < 2; ++ch) buffer.addFrom (ch, 0, fxBus, ch, 0, numSamples);
        master.process (buffer, numSamples, masterParams);
    }

    // Mutate auto : une demande au thread message toutes les N mesures (4 temps)
    if (mutateOnParam->load() > 0.5f && (playing || numHeld > 0))
    {
        const auto bar = (juce::int64) std::floor (blockStartBeat / 4.0);
        if (bar != lastMutateBar)
        {
            if (lastMutateBar >= 0 && bar % juce::jmax (1, (int) mutateBarsParam->load()) == 0)
                randomManager->mutateRequests.fetch_add (1);
            lastMutateBar = bar;
        }
    }
    else
    {
        lastMutateBar = -1;
    }

    sampleCounter += numSamples;
    freeBeat = playing ? ppq + numSamples * beatsPerSample : freeBeat + numSamples * beatsPerSample;

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    outputGain.applyGain (buffer, numSamples);

    // Resample interne : enregistre la sortie finale du plugin
    recorder.process (buffer, numSamples, blockStartBeat, beatsPerSample, playing);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = ch < buffer.getNumChannels() ? buffer.getMagnitude (ch, 0, numSamples) : 0.0f;
        const float prev = outputLevel[(size_t) ch].load (std::memory_order_relaxed);
        outputLevel[(size_t) ch].store (juce::jmax (peak, prev * 0.9f), std::memory_order_relaxed);
    }
}

void IDGemsSlicerProcessor::readFxParams() noexcept
{
    for (int i = 0; i < idg::numFxModules; ++i)
    {
        fxParams.on[(size_t) i] = fxOnParams[(size_t) i]->load() > 0.5f;
        fxParams.mix[(size_t) i] = fxMixParams[(size_t) i]->load() * 0.01f;
    }
    fxParams.crushBits = crushBitsParam->load();
    fxParams.crushDownsample = crushDownParam->load();
    fxParams.filterType = (int) fxFilterTypeParam->load();
    fxParams.filterCutoff = fxCutoffParam->load();
    fxParams.filterReso = fxResoParam->load();
    fxParams.tapeEngaged = tapeStopParam->load() > 0.5f;
    fxParams.tapeSeconds = tapeTimeParam->load() * 0.001f;
    fxParams.stutterEngaged = stutterParam->load() > 0.5f;
    fxParams.stutterDivision = (int) stutterDivParam->load();
    fxParams.stutterChance = stutterChanceParam->load() * 0.01f;
    fxParams.grainDensity = grainDensityParam->load();
    fxParams.grainSize = grainSizeParam->load() * 0.001f;
    fxParams.grainSpray = grainSprayParam->load() * 0.001f;
    fxParams.grainPitch = grainPitchParam->load();

    masterParams.ottDepth = ottDepthParam->load() * 0.01f;
    masterParams.ottTime = ottTimeParam->load() * 0.01f;
    masterParams.ottUp = ottUpParam->load() * 0.01f;
    masterParams.ottDown = ottDownParam->load() * 0.01f;
    masterParams.clipOn = clipOnParam->load() > 0.5f;
    masterParams.clipDrive = clipDriveParam->load() * 0.01f;
    masterParams.mix = masterMixParam->load() * 0.01f;
    masterParams.limiterOn = limiterOnParam->load() > 0.5f;
    masterParams.ceilingDb = ceilingParam->load();
}

juce::File IDGemsSlicerProcessor::getExportFolder() const
{
    const auto path = apvts.state.getProperty (IDs::exportFolder).toString();
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path)) return juce::File (path);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Slicer");
}

void IDGemsSlicerProcessor::setExportFolder (const juce::File& f)
{
    apvts.state.setProperty (IDs::exportFolder, f.getFullPathName(), nullptr);
}

void IDGemsSlicerProcessor::prepareOfflineCopy (IDGemsSlicerProcessor& src)
{
    apvts.replaceState (src.apvts.copyState());
    patternStore.setBank (src.patternStore.getBank());
    patternStore.selectPattern (src.patternStore.getCurrentIndex());
    mapStore.setPath (src.mapStore.get());
    setFxOrder (src.getFxOrder());
    sampleManager.adoptFrom (src.sampleManager);
    setOfflineBpm (src.getHostBpm());

    // Pas de mutation / morph pendant un rendu
    for (auto id : { "mutateOn", "morphOn" })
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (0.0f);
}

void IDGemsSlicerProcessor::setFxOrder (const std::array<int, idg::numFxModules>& o)
{
    const int v = idg::FxChain::encodeOrder (o);
    fxChain.order.store (idg::FxChain::encodeOrder (idg::FxChain::decodeOrder (v)));
    apvts.state.setProperty (IDs::fxOrder, fxChain.order.load(), nullptr);
}

//==============================================================================
juce::AudioProcessorEditor* IDGemsSlicerProcessor::createEditor()
{
    return new IDGemsSlicerEditor (*this);
}

juce::Point<int> IDGemsSlicerProcessor::getSavedEditorSize() const
{
    return { (int) apvts.state.getProperty (IDs::editorWidth,  baseWidth),
             (int) apvts.state.getProperty (IDs::editorHeight, baseHeight) };
}

void IDGemsSlicerProcessor::setSavedEditorSize (int w, int h)
{
    apvts.state.setProperty (IDs::editorWidth,  w, nullptr);
    apvts.state.setProperty (IDs::editorHeight, h, nullptr);
}

int IDGemsSlicerProcessor::getSelectedSlot() const
{
    return juce::jlimit (0, idg::numSlots - 1, (int) apvts.state.getProperty (IDs::selectedSlot, 0));
}

void IDGemsSlicerProcessor::setSelectedSlot (int s)
{
    apvts.state.setProperty (IDs::selectedSlot, juce::jlimit (0, idg::numSlots - 1, s), nullptr);
}

int IDGemsSlicerProcessor::getUiProperty (const juce::Identifier& id, int def) const
{
    return (int) apvts.state.getProperty (id, def);
}

void IDGemsSlicerProcessor::setUiProperty (const juce::Identifier& id, int value)
{
    apvts.state.setProperty (id, value, nullptr);
}

//==============================================================================
juce::ValueTree IDGemsSlicerProcessor::createStateTree()
{
    auto state = apvts.copyState();
    state.setProperty (IDs::stateVersion, currentStateVersion, nullptr);

    for (auto id : { IDs::slots, IDs::sequencer, IDs::mapPath, IDs::random })
        state.removeChild (state.getChildWithName (id), nullptr);
    state.appendChild (sampleManager.toValueTree(), nullptr);
    state.appendChild (patternStore.toValueTree(), nullptr);
    state.appendChild (mapStore.toValueTree(), nullptr);
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        state.appendChild (randomManager->toValueTree(), nullptr);
    return state;
}

void IDGemsSlicerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void IDGemsSlicerProcessor::applyStateTree (juce::ValueTree tree, bool keepEditorState)
{
    if (! tree.hasType (apvts.state.getType())) return;

    // Chargement d'un preset : on garde la fenetre et les reglages d'interface actuels
    if (keepEditorState)
        for (auto id : { IDs::editorWidth, IDs::editorHeight, IDs::selectedSlot, IDs::exportFolder,
                         juce::Identifier ("selectedTab"), juce::Identifier ("recordBars"), juce::Identifier ("exportBars") })
            if (apvts.state.hasProperty (id)) tree.setProperty (id, apvts.state.getProperty (id), nullptr);

    auto slots = tree.getChildWithName (IDs::slots);
    auto seq = tree.getChildWithName (IDs::sequencer);
    auto path = tree.getChildWithName (IDs::mapPath);
    auto rnd = tree.getChildWithName (IDs::random);
    for (auto c : { slots, seq, path, rnd }) tree.removeChild (c, nullptr);

    apvts.replaceState (tree);
    if (tree.hasProperty (IDs::fxOrder))
        setFxOrder (idg::FxChain::decodeOrder ((int) tree.getProperty (IDs::fxOrder)));
    sampleManager.fromValueTree (slots.isValid() ? slots : juce::ValueTree (IDs::slots));
    patternStore.fromValueTree (seq.isValid() ? seq : juce::ValueTree (IDs::sequencer));
    mapStore.fromValueTree (path.isValid() ? path : juce::ValueTree (IDs::mapPath));
    if (rnd.isValid())
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            randomManager->fromValueTree (rnd);
        else
            juce::MessageManager::callAsync ([w = juce::WeakReference<idg::RandomManager> (randomManager.get()), rnd]
            {
                if (auto* rm = w.get()) rm->fromValueTree (rnd);
            });
    }
}

void IDGemsSlicerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateTree (juce::ValueTree::fromXml (*xml), false);

    if (auto* ed = dynamic_cast<IDGemsSlicerEditor*> (getActiveEditor()))
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<IDGemsSlicerEditor> (ed)]
        {
            if (safe != nullptr) safe->restoreSavedSize();
        });
}

juce::String IDGemsSlicerProcessor::getPresetName() const
{
    return apvts.state.getProperty (IDs::presetName, "Init").toString();
}

void IDGemsSlicerProcessor::setPresetName (const juce::String& n)
{
    apvts.state.setProperty (IDs::presetName, n, nullptr);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IDGemsSlicerProcessor();
}
