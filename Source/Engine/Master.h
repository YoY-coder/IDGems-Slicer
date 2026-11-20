#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace idg
{
//==============================================================================
// Chaine master : OTT (compression 3 bandes haut + bas) -> soft clip ->
// dry/wet global -> limiteur. Tout pre-alloue dans prepare().
//==============================================================================
struct MasterParams
{
    float ottDepth = 0.0f;        // 0..1 (dry/wet de l'OTT)
    float ottTime = 0.5f;         // 0..1 (rapide -> lent)
    float ottUp = 1.0f, ottDown = 1.0f;   // 0..1 : quantites de compression vers le haut / le bas
    bool clipOn = false; float clipDrive = 0.0f;   // drive 0..1
    float mix = 1.0f;             // dry/wet global de la chaine master
    bool limiterOn = true; float ceilingDb = -0.3f;
};

class Master
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>&, int numSamples, const MasterParams&) noexcept;
    void reset() noexcept;

private:
    double sampleRate = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> lowLP, lowHP, highLP, highHP;
    juce::AudioBuffer<float> bands[3], dry;
    std::array<float, 3> env {};
    juce::dsp::Limiter<float> limiter;
    float limiterCeilingSet = 1.0e9f;
};
}
