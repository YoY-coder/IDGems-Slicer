#pragma once

#include "SampleData.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace idg
{
//==============================================================================
// Lecteur de slices polyphonique. Toutes les voix (et leurs filtres) sont
// pre-allouees. Une voix = une slice d'UN seul sample.
//
// Par voix : ADSR, pitch "clic", start / longueur / reverse, filtre LP/HP/BP
// avec drive, Slice Freeze (un grain de la slice joue en boucle).
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
        bool reverse = false;
        float startFraction = 0.0f;    // point de depart dans la slice (0..1)
        float lengthFraction = 1.0f;   // portion de la slice jouee (0..1)
        int gateSamples = -1;          // > 0 : relachement automatique apres N samples (sequenceur)
        juce::ADSR::Parameters adsr { 0.001f, 0.1f, 1.0f, 0.03f };
        float clickSemitones = 0.0f;   // pitch env "clic"
        float clickSeconds = 0.02f;
        bool freeze = false;
    };

    // Reglages lus a chaque bloc (modulables en temps reel)
    struct LiveParams
    {
        int filterType = 0;            // 0 off, 1 LP, 2 HP, 3 BP
        float cutoff = 20000.0f, resonance = 0.7f, drive = 0.0f;
        float freezePosition = 0.0f;   // 0..1 dans la slice
        float freezeSeconds = 0.08f;   // taille du grain
        float freezeFade = 0.3f;       // 0..1 : part du grain en fondu
    };

    void prepare (double sampleRate, int maxBlockSize);
    void noteOn (const SampleData::Ptr& data, const NoteParams&) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff (bool immediate) noexcept;
    void render (juce::AudioBuffer<float>& out, int start, int num, const LiveParams&) noexcept;

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
        bool active = false, held = false, stealing = false;
        SampleData::Ptr data;
        int slot = -1, note = -1;
        double pos = 0.0, inc = 1.0, startPos = 0.0;
        int start = 0, end = 0;                 // zone jouee (apres longueur)
        int sliceStart = 0, sliceEnd = 0;       // slice complete (pour le freeze)
        float gain = 1.0f, fadeLen = 0.0f;
        float steal = 1.0f, stealStep = 0.0f;   // fondu rapide de vol de voix
        bool reverse = false;
        int gateRemaining = -1;
        juce::uint64 age = 0;
        juce::ADSR adsr;
        float clickSemis = 0.0f, clickEnv = 0.0f, clickDecay = 0.0f;
        bool freeze = false;
        double grainPhase = 0.0;
        float played = 0.0f;                    // samples (source) deja lus : fondu d'entree
        juce::int64 oneShotRemaining = -1;      // duree max d'un freeze en one-shot
        juce::dsp::StateVariableTPTFilter<float> filter;
        int filterTypeSet = -1;
    };

    void steal (Voice&) noexcept;
    void release (Voice&) noexcept;
    void stopVoice (Voice&) noexcept;

    std::array<Voice, maxVoices> voices;
    double sampleRate = 44100.0;
    juce::uint64 ageCounter = 0;
};
}
