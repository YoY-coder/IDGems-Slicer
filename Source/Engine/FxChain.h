#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace idg
{
//==============================================================================
// Chaine FX IDGems : 5 modules dont l'ordre est modifiable.
// Tout est pre-alloue dans prepare() : aucune allocation dans process().
//==============================================================================
enum FxModuleId { fxBitcrush = 0, fxFilter, fxTapeStop, fxStutter, fxGranular, numFxModules };

const juce::StringArray& fxModuleNames();

struct FxParams
{
    std::array<bool, numFxModules> on {};
    std::array<float, numFxModules> mix {};

    float crushBits = 8.0f, crushDownsample = 1.0f;
    int filterType = 0; float filterCutoff = 1000.0f, filterReso = 1.0f;
    bool tapeEngaged = false; float tapeSeconds = 0.6f;
    bool stutterEngaged = false; int stutterDivision = 2; float stutterChance = 0.0f;
    float grainDensity = 15.0f, grainSize = 0.12f, grainSpray = 0.5f, grainPitch = 0.0f;
};

struct TimeInfo
{
    double blockStartBeat = 0.0;   // position (en temps) du debut du bloc
    double beatsPerSample = 0.0;
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>&, int numSamples, const FxParams&, const TimeInfo&) noexcept;

    // Ordre des modules : 5 indices encodes dans un entier (4 bits chacun)
    static int encodeOrder (const std::array<int, numFxModules>&);
    static std::array<int, numFxModules> decodeOrder (int);
    std::atomic<int> order { encodeOrder ({ 0, 1, 2, 3, 4 }) };

    // Etat pour l'affichage
    std::atomic<bool> stutterActive { false }, tapeActive { false };

private:
    struct Ring
    {
        juce::AudioBuffer<float> data;
        int size = 0, write = 0;
        void prepare (int samples) { size = samples; data.setSize (2, samples); data.clear(); write = 0; }
        void push (float l, float r) noexcept { data.setSample (0, write, l); data.setSample (1, write, r); if (++write >= size) write = 0; }
        float read (int ch, double pos) const noexcept
        {
            while (pos < 0) pos += size;
            const int i0 = (int) pos % size;
            const int i1 = (i0 + 1) % size;
            const float t = (float) (pos - std::floor (pos));
            return data.getSample (ch, i0) * (1.0f - t) + data.getSample (ch, i1) * t;
        }
    };

    void processBitcrush (float* l, float* r, int n, const FxParams&) noexcept;
    void processFilter (float* l, float* r, int n, const FxParams&) noexcept;
    void processTapeStop (float* l, float* r, int n, const FxParams&) noexcept;
    void processStutter (float* l, float* r, int n, const FxParams&, const TimeInfo&) noexcept;
    void processGranular (float* l, float* r, int n, const FxParams&) noexcept;

    double sampleRate = 44100.0;
    juce::AudioBuffer<float> scratch;   // copie seche pour le dry/wet de chaque module
    juce::Random rng;

    // Bitcrush
    float crushHold[2] {}; float crushCounter = 0.0f;

    // Filtre
    juce::dsp::StateVariableTPTFilter<float> filter;
    int filterTypeSet = -1;

    // Tape stop
    Ring tapeRing;
    double tapeRead = 0.0, tapeSpeed = 1.0;
    bool tapeWasEngaged = false;
    float tapeReturn = 1.0f;            // fondu de retour au direct

    // Stutter
    Ring stutterRing;
    bool stutterOn = false;
    double stutterStart = 0.0, stutterLen = 1.0, stutterPhase = 0.0;
    juce::int64 lastBeatIndex = -1;
    bool autoStutter = false;
    float stutterFade = 0.0f;

    // Granulaire
    Ring grainRing;
    struct Grain { bool active = false; double pos = 0.0, inc = 1.0; int length = 0, age = 0; float panL = 1.0f, panR = 1.0f; };
    std::array<Grain, 32> grains;
    double grainTimer = 0.0;
};
}
