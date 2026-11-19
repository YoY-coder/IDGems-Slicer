#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer.h"
#include "SampleManager.h"
#include <map>

namespace idg
{
//==============================================================================
// Systeme de random (thread message uniquement) :
//  - random general ou par section, dosage, seed reproductible
//  - plage min/max et cadenas par parametre, cadenas par section
//  - modes "tout" / "garder le groove" / "garder les sons"
//  - historique (retour / avant)
//  - mutate automatique (demande par le thread audio a chaque N mesures)
//  - morph pad XY entre 4 snapshots
//==============================================================================
enum RandomSection { secRouting = 0, secSlicing, secMap, secSequencer, secVoice, secFx, numSections };

class RandomManager final : public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    RandomManager (juce::AudioProcessorValueTreeState&, PatternStore&, SampleManager&);
    ~RandomManager() override;

    struct ParamEntry
    {
        juce::String id, name;
        int section = 0;
        bool groove = false;     // fait partie du "rythme" (garder le groove)
        bool isLane = false;     // lane du sequenceur
        int lane = -1;
    };

    struct ParamSettings { bool locked = false; float min = 0.0f, max = 1.0f; };

    static const juce::StringArray& sectionNames();
    const std::vector<ParamEntry>& getEntries() const noexcept { return entries; }

    ParamSettings getSettings (const juce::String& id) const;
    void setSettings (const juce::String& id, const ParamSettings&);
    bool isSectionLocked (int s) const noexcept { return sectionLocks[(size_t) s]; }
    void setSectionLocked (int s, bool l) { sectionLocks[(size_t) s] = l; sendChangeMessage(); }

    // Actions
    void randomiseAll();
    void randomiseSection (int section);
    void mutate (float rate);

    // Seed
    int getSeed() const noexcept { return seed; }
    void setSeed (int s) { seed = s; sendChangeMessage(); }
    bool fixedSeed = false;

    // Historique
    bool canGoBack() const noexcept { return historyIndex > 0 || ! history.empty(); }
    bool canGoForward() const noexcept { return historyIndex + 1 < (int) history.size(); }
    void goBack();
    void goForward();
    juce::String historyText() const;

    // Morph
    void captureSnapshot (int corner);
    bool hasSnapshot (int corner) const noexcept { return morphSnapshots[(size_t) corner].captured; }
    void applyMorph (float x, float y);

    // Demandes de mutation venant du thread audio
    std::atomic<int> mutateRequests { 0 };

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

private:
    struct Snapshot
    {
        juce::ValueTree params;
        Pattern pattern;
        std::array<SliceSettings, numSlots> slicing;
    };

    struct MorphSnapshot
    {
        bool captured = false;
        std::vector<float> values;      // valeurs normalisees des parametres (ordre de "entries")
        Pattern pattern;
    };

    void timerCallback() override;
    void buildEntries();
    void run (int sectionMask, float amount, bool isMutation);
    bool allowed (const ParamEntry&, int sectionMask) const;
    Snapshot capture() const;
    void restore (const Snapshot&);
    void pushHistory();
    void setParam (juce::RangedAudioParameter*, float normalised);

    juce::AudioProcessorValueTreeState& apvts;
    PatternStore& patterns;
    SampleManager& samples;

    std::vector<ParamEntry> entries;
    std::map<juce::String, ParamSettings> settings;
    std::array<bool, numSections> sectionLocks {};

    int seed = 1;
    std::vector<Snapshot> history;
    int historyIndex = -1;

    std::array<MorphSnapshot, 4> morphSnapshots;
    float lastMorphX = -1.0f, lastMorphY = -1.0f;
    int lastMutateRequests = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (RandomManager)
};
}
