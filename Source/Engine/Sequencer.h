#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

namespace idg
{
//==============================================================================
// Sequenceur : une lane par parametre, chacune avec sa propre longueur
// (polyrythmie). Les donnees sont editees dans l'interface et copiees vers
// le thread audio sans allocation ni blocage.
//==============================================================================
constexpr int maxSteps = 64;
constexpr int numPatterns = 8;   // banque de patterns par preset

enum LaneId
{
    laneSample = 0, laneSlice, lanePitch, laneReverse, laneGate, laneStart,
    laneVelocity, laneRatchet, laneProbability, laneFreeze, laneFxSend,
    numLanes
};

struct LaneInfo
{
    const char* name;
    const char* tooltip;
    float minValue, maxValue, defaultValue;
    bool discrete;          // valeurs entieres
    bool enabledByDefault;
};

const LaneInfo& laneInfo (int lane);
juce::String formatLaneValue (int lane, float value);

struct Lane
{
    bool enabled = true;
    int length = 16;
    std::array<float, maxSteps> values {};
};

struct Pattern
{
    std::array<Lane, numLanes> lanes;

    Pattern();
    float valueAt (int lane, juce::int64 globalStep) const noexcept;   // tient compte de on/off et longueur
};

// Resolutions du sequenceur (en temps)
const juce::StringArray& seqRateNames();
double seqRateInBeats (int index);

// Grooves : decalage temporel (fraction de pas) et accent de velocite sur 16 pas
const juce::StringArray& grooveNames();
float grooveTiming (int groove, juce::int64 step);
float grooveVelocity (int groove, juce::int64 step);

//==============================================================================
using PatternBank = std::array<Pattern, numPatterns>;

class PatternStore
{
public:
    PatternStore() = default;

    // Pattern courant de la banque (change par l'interface ou au MIDI)
    int getCurrentIndex() const noexcept { return current.load(); }
    void selectPattern (int index) noexcept { current.store (juce::jlimit (0, numPatterns - 1, index)); }
    const PatternBank& getBank() const noexcept { return bank; }
    void setBank (const PatternBank&);
    void copyPattern (int from, int to);

    // Thread message
    const Pattern& get() const noexcept { return bank[(size_t) current.load()]; }
    void setValue (int lane, int step, float value);
    void setLength (int lane, int length);
    void setEnabled (int lane, bool enabled);
    void resetLane (int lane);
    void randomiseLane (int lane, juce::Random&);
    void randomiseAll (juce::Random&);
    // Re-tire une fraction "amount" des pas de la lane (mutation douce)
    void mutateLane (int lane, juce::Random&, float amount);
    static float randomValue (int lane, juce::Random&);
    void setPattern (const Pattern&);

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    // Thread audio : copie la derniere version de la banque si elle a change
    void pull (PatternBank& dest, int& lastVersion) noexcept;

private:
    void publish();
    Pattern& cur() noexcept { return bank[(size_t) current.load()]; }

    PatternBank bank;
    PatternBank shared;
    std::atomic<int> current { 0 };
    mutable juce::SpinLock lock;
    std::atomic<int> version { 1 };
};
}
