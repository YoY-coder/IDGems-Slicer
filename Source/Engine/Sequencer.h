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
class PatternStore
{
public:
    PatternStore() = default;

    // Thread message
    const Pattern& get() const noexcept { return edit; }
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

    // Thread audio : copie la derniere version si elle a change
    void pull (Pattern& dest, int& lastVersion) noexcept;

private:
    void publish();

    Pattern edit;
    Pattern shared;
    mutable juce::SpinLock lock;
    std::atomic<int> version { 1 };
};
}
