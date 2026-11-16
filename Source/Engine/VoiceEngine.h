#pragma once

#include "SampleData.h"
#include <array>
#include <atomic>

namespace idg
{
//==============================================================================
// Lecteur de slices polyphonique. Toutes les voix sont pre-allouees.
// Une voix = une slice d'UN seul sample (jamais de superposition par declenchement).
//==============================================================================
class VoiceEngine
{
public:
    static constexpr int maxVoices = 32;

    struct NoteParams
    {
        int slot = 0, slice = 0, note = -1;
        float velocity = 1.0f;
        double pitchRatio = 1.0;       // inclut la conversion de frequence d'echantillonnage
        bool oneShot = false;
        int polyphony = 8;
        double fadeSeconds = 0.003;
        bool normalise = false;
    };

    void prepare (double sampleRate);
    void noteOn (const SampleData::Ptr& data, const NoteParams&) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff (bool immediate) noexcept;
    void render (juce::AudioBuffer<float>& out, int start, int num) noexcept;

    // Pour l'affichage (lu par l'interface)
    struct Display
    {
        std::atomic<int>   slot { -1 };
        std::atomic<float> position { 0.0f };   // 0..1 dans le fichier
    };
    std::array<Display, maxVoices> display;

private:
    struct Voice
    {
        bool active = false, held = false, releasing = false;
        SampleData::Ptr data;
        int slot = -1, note = -1;
        double pos = 0.0, inc = 1.0;
        int start = 0, end = 0;
        float gain = 1.0f, fadeLen = 0.0f;
        float release = 1.0f, releaseStep = 0.0f;
        juce::uint64 age = 0;
    };

    void startRelease (Voice&, double seconds) noexcept;
    void stopVoice (Voice&) noexcept;

    std::array<Voice, maxVoices> voices;
    double sampleRate = 44100.0;
    juce::uint64 ageCounter = 0;
};
}
