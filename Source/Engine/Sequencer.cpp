#include "Sequencer.h"

namespace idg
{
namespace
{
    const LaneInfo lanesTable[numLanes] =
    {
        { "SAMPLE",   "Sample joue sur ce pas : Auto (aiguillage) ou force sur 1, 2, 3, 4",          0.0f, 4.0f, 0.0f, true,  true  },
        { "SLICE",    "Position du Slice Select sur ce pas (remplace le curseur SLICE SELECT)",       0.0f, 1.0f, 0.0f, false, false },
        { "PITCH",    "Transposition du pas, en demi-tons",                                          -24.0f, 24.0f, 0.0f, true,  true  },
        { "REVERSE",  "Lecture a l'envers sur ce pas",                                                0.0f, 1.0f, 0.0f, true,  true  },
        { "GATE",     "Duree de la note, en fraction du pas",                                         0.05f, 1.0f, 0.5f, false, true  },
        { "START",    "Point de depart dans la slice",                                                0.0f, 0.95f, 0.0f, false, true  },
        { "VELOCITE", "Volume du pas",                                                                0.0f, 1.0f, 1.0f, false, true  },
        { "RATCHET",  "Nombre de repetitions dans le pas",                                            1.0f, 8.0f, 1.0f, true,  true  },
        { "PROBA",    "Probabilite que le pas se declenche",                                          0.0f, 1.0f, 1.0f, false, true  },
        { "FREEZE",   "Slice Freeze sur ce pas (fige un grain et le boucle, voir onglet VOIX)",                                  0.0f, 1.0f, 0.0f, true,  true  },
        { "ENVOI FX", "Envoi vers la chaine FX sur ce pas (lane active = remplace le reglage ENVOI FX global)", 0.0f, 1.0f, 1.0f, false, false },
    };

    namespace IDs
    {
        const juce::Identifier sequencer { "Sequencer" };
        const juce::Identifier lane      { "Lane" };
        const juce::Identifier index     { "index" };
        const juce::Identifier enabled   { "enabled" };
        const juce::Identifier length    { "length" };
        const juce::Identifier values    { "values" };
    }

    // 16 pas, decalage en fraction de pas
    const float grooveTimingTable[5][16] =
    {
        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 },                                         // Aucun
        { 0,.18f,0,.12f, 0,.20f,0,.10f, 0,.18f,0,.12f, 0,.22f,0,.08f },                 // Shuffle
        { 0,-.10f,-.04f,-.12f, 0,-.10f,-.04f,-.12f, 0,-.10f,-.04f,-.12f, 0,-.10f,-.04f,-.12f }, // Pousse
        { 0,.06f,.12f,.16f, .04f,.10f,.14f,.20f, 0,.06f,.12f,.16f, .04f,.10f,.14f,.20f }, // Paresseux
        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 },                                         // Accents
    };
    const float grooveVelocityTable[5][16] =
    {
        { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 },
        { 1,.8f,.9f,.75f, 1,.8f,.9f,.75f, 1,.8f,.9f,.75f, 1,.8f,.9f,.75f },
        { 1,.9f,.95f,.85f, 1,.9f,.95f,.85f, 1,.9f,.95f,.85f, 1,.9f,.95f,.85f },
        { 1,.85f,.9f,.8f, .95f,.85f,.9f,.8f, 1,.85f,.9f,.8f, .95f,.85f,.9f,.8f },
        { 1,.55f,.7f,.55f, .9f,.55f,.7f,.55f, 1,.55f,.7f,.55f, .9f,.55f,.7f,.6f },
    };
}

const LaneInfo& laneInfo (int lane) { return lanesTable[juce::jlimit (0, numLanes - 1, lane)]; }

juce::String formatLaneValue (int lane, float v)
{
    switch (lane)
    {
        case laneSample:   return (int) v == 0 ? juce::String ("Auto") : "S" + juce::String ((int) v);
        case lanePitch:    return (v > 0 ? "+" : "") + juce::String ((int) v) + " st";
        case laneReverse:
        case laneFreeze:   return v > 0.5f ? "ON" : "off";
        case laneRatchet:  return "x" + juce::String ((int) v);
        default:           return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }
}

Pattern::Pattern()
{
    for (int i = 0; i < numLanes; ++i)
    {
        auto& l = lanes[(size_t) i];
        l.enabled = laneInfo (i).enabledByDefault;
        l.length = 16;
        l.values.fill (laneInfo (i).defaultValue);
    }
}

float Pattern::valueAt (int lane, juce::int64 globalStep) const noexcept
{
    const auto& l = lanes[(size_t) lane];
    if (! l.enabled) return lanesTable[lane].defaultValue;
    const int len = juce::jlimit (1, maxSteps, l.length);
    return l.values[(size_t) (globalStep % len)];
}

const juce::StringArray& seqRateNames()
{
    static const juce::StringArray n { "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T" };
    return n;
}

double seqRateInBeats (int i)
{
    static const double r[] { 1.0, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return r[juce::jlimit (0, 7, i)];
}

const juce::StringArray& grooveNames()
{
    static const juce::StringArray n { "Aucun", "Shuffle", "Pousse", "Paresseux", "Accents" };
    return n;
}

float grooveTiming (int g, juce::int64 step)   { return grooveTimingTable[juce::jlimit (0, 4, g)][step & 15]; }
float grooveVelocity (int g, juce::int64 step) { return grooveVelocityTable[juce::jlimit (0, 4, g)][step & 15]; }

//==============================================================================
void PatternStore::publish()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        shared = edit;
    }
    version.fetch_add (1);
}

void PatternStore::pull (Pattern& dest, int& lastVersion) noexcept
{
    const int v = version.load();
    if (v == lastVersion) return;
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked())
    {
        dest = shared;
        lastVersion = v;
    }
}

void PatternStore::setValue (int lane, int step, float value)
{
    if (! juce::isPositiveAndBelow (lane, numLanes) || ! juce::isPositiveAndBelow (step, maxSteps)) return;
    const auto& info = laneInfo (lane);
    value = juce::jlimit (info.minValue, info.maxValue, value);
    if (info.discrete) value = (float) juce::roundToInt (value);
    edit.lanes[(size_t) lane].values[(size_t) step] = value;
    publish();
}

void PatternStore::setLength (int lane, int length)
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return;
    edit.lanes[(size_t) lane].length = juce::jlimit (1, maxSteps, length);
    publish();
}

void PatternStore::setEnabled (int lane, bool e)
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return;
    edit.lanes[(size_t) lane].enabled = e;
    publish();
}

void PatternStore::resetLane (int lane)
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return;
    edit.lanes[(size_t) lane].values.fill (laneInfo (lane).defaultValue);
    publish();
}

void PatternStore::randomiseLane (int lane, juce::Random& r)
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return;
    const auto& info = laneInfo (lane);
    auto& l = edit.lanes[(size_t) lane];
    for (auto& v : l.values)
    {
        float x = r.nextFloat();
        switch (lane)
        {
            // Des tirages "musicaux" plutot qu'uniformes
            case laneReverse:     x = x < 0.2f ? 1.0f : 0.0f; break;
            case laneFreeze:      x = x < 0.12f ? 1.0f : 0.0f; break;
            case lanePitch:       x = r.nextFloat() < 0.6f ? 0.5f : x; break;                      // souvent 0 st
            case laneRatchet:     x = r.nextFloat() < 0.7f ? 0.0f : x * 0.45f; break;              // souvent x1
            case laneProbability: x = 0.45f + 0.55f * x; break;
            case laneVelocity:    x = 0.35f + 0.65f * x; break;
            case laneGate:        x = 0.15f + 0.85f * x; break;
            case laneStart:       x = r.nextFloat() < 0.6f ? 0.0f : x * 0.6f; break;
            default: break;
        }
        v = info.minValue + x * (info.maxValue - info.minValue);
        if (info.discrete) v = (float) juce::roundToInt (v);
    }
    l.enabled = true;
    publish();
}

void PatternStore::randomiseAll (juce::Random& r)
{
    for (int i = 0; i < numLanes; ++i)
        if (i != laneFreeze && i != laneFxSend)
        {
            const bool wasEnabled = edit.lanes[(size_t) i].enabled;
            randomiseLane (i, r);
            edit.lanes[(size_t) i].enabled = wasEnabled;
        }
    publish();
}

void PatternStore::setPattern (const Pattern& p)
{
    edit = p;
    publish();
}

juce::ValueTree PatternStore::toValueTree() const
{
    juce::ValueTree t (IDs::sequencer);
    Pattern copy;   // copie coherente (peut etre appele hors du thread message)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        copy = shared;
    }
    for (int i = 0; i < numLanes; ++i)
    {
        const auto& l = copy.lanes[(size_t) i];
        juce::ValueTree lt (IDs::lane);
        lt.setProperty (IDs::index, i, nullptr);
        lt.setProperty (IDs::enabled, l.enabled, nullptr);
        lt.setProperty (IDs::length, l.length, nullptr);
        juce::StringArray vals;
        for (auto v : l.values) vals.add (juce::String (v, 4));
        lt.setProperty (IDs::values, vals.joinIntoString (","), nullptr);
        t.appendChild (lt, nullptr);
    }
    return t;
}

void PatternStore::fromValueTree (const juce::ValueTree& t)
{
    Pattern p;
    for (int i = 0; i < numLanes; ++i)
    {
        auto lt = t.getChildWithProperty (IDs::index, i);
        if (! lt.isValid()) continue;
        auto& l = p.lanes[(size_t) i];
        l.enabled = (bool) lt.getProperty (IDs::enabled, l.enabled);
        l.length = juce::jlimit (1, maxSteps, (int) lt.getProperty (IDs::length, 16));
        const auto vals = juce::StringArray::fromTokens (lt.getProperty (IDs::values).toString(), ",", "");
        for (int s = 0; s < juce::jmin (maxSteps, vals.size()); ++s)
            l.values[(size_t) s] = juce::jlimit (laneInfo (i).minValue, laneInfo (i).maxValue, vals[s].getFloatValue());
    }
    setPattern (p);
}
}
