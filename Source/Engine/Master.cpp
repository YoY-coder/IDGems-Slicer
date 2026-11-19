#include "Master.h"

namespace idg
{
void Master::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    const int n = juce::jmax (512, maxBlockSize);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) n, 2 };

    for (auto* f : { &lowLP, &lowHP, &highLP, &highHP }) { f->prepare (spec); f->reset(); }
    lowLP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    lowHP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    highLP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    highHP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lowLP.setCutoffFrequency (120.0f);  lowHP.setCutoffFrequency (120.0f);
    highLP.setCutoffFrequency (2500.0f); highHP.setCutoffFrequency (2500.0f);

    for (auto& b : bands) b.setSize (2, n);
    dry.setSize (2, n);
    env.fill (0.0f);

    limiter.prepare (spec);
    limiter.setRelease (80.0f);
    limiterCeilingSet = 1.0e9f;
}

void Master::process (juce::AudioBuffer<float>& buf, int n, const MasterParams& p) noexcept
{
    if (buf.getNumChannels() < 2 || n <= 0 || n > dry.getNumSamples()) return;

    for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, buf, ch, 0, n);

    // OTT : 3 bandes, compression vers le bas au-dessus du seuil haut, vers le haut sous le seuil bas
    if (p.ottDepth > 0.0f)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = buf.getReadPointer (ch);
            float* lo = bands[0].getWritePointer (ch);
            float* mid = bands[1].getWritePointer (ch);
            float* hi = bands[2].getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = in[i];
                lo[i] = lowLP.processSample (ch, x);
                const float rest = lowHP.processSample (ch, x);
                mid[i] = highLP.processSample (ch, rest);
                hi[i] = highHP.processSample (ch, rest);
            }
        }

        const float attack = std::exp (-1.0f / (float) ((0.001 + 0.009 * p.ottTime) * sampleRate));
        const float release = std::exp (-1.0f / (float) ((0.03 + 0.25 * p.ottTime) * sampleRate));
        const float upperDb = -12.0f, lowerDb = -40.0f;
        const float downRatio = 1.0f + 5.0f * p.ottDown;   // jusqu'a 6:1
        const float upAmount = 0.75f * p.ottUp;            // fraction de l'ecart remontee
        const float makeup[3] { 1.4f, 1.2f, 1.4f };

        for (int b = 0; b < 3; ++b)
        {
            float* L = bands[b].getWritePointer (0);
            float* R = bands[b].getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float level = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                const float coeff = level > env[(size_t) b] ? attack : release;
                env[(size_t) b] = level + coeff * (env[(size_t) b] - level);
                const float db = juce::Decibels::gainToDecibels (env[(size_t) b], -90.0f);

                float gainDb = 0.0f;
                if (db > upperDb)       gainDb = (upperDb + (db - upperDb) / downRatio) - db;
                else if (db < lowerDb && db > -80.0f) gainDb = juce::jmin (24.0f, (lowerDb - db) * upAmount);

                const float g = juce::Decibels::decibelsToGain (gainDb) * makeup[b];
                L[i] *= g;
                R[i] *= g;
            }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            float* out = buf.getWritePointer (ch);
            const float* d = dry.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float wet = bands[0].getSample (ch, i) + bands[1].getSample (ch, i) + bands[2].getSample (ch, i);
                out[i] = d[i] + (wet - d[i]) * p.ottDepth;
            }
        }
    }

    // Soft clip
    if (p.clipOn)
    {
        const float drive = 1.0f + p.clipDrive * 15.0f;
        const float comp = 1.0f / std::tanh (drive);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* out = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i) out[i] = std::tanh (out[i] * drive) * comp * (1.0f / (1.0f + p.clipDrive));
        }
    }

    // Dry / wet global (avant le limiteur de securite)
    if (p.mix < 1.0f)
        for (int ch = 0; ch < 2; ++ch)
        {
            float* out = buf.getWritePointer (ch);
            const float* d = dry.getReadPointer (ch);
            for (int i = 0; i < n; ++i) out[i] = d[i] + (out[i] - d[i]) * p.mix;
        }

    // Limiteur
    if (p.limiterOn)
    {
        if (std::abs (limiterCeilingSet - p.ceilingDb) > 0.01f)
        {
            limiter.setThreshold (p.ceilingDb);
            limiterCeilingSet = p.ceilingDb;
        }
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), 2, (size_t) n);
        limiter.process (juce::dsp::ProcessContextReplacing<float> (block));

        // Garde-fou : jamais au-dessus du plafond, quoi qu'il arrive
        const float ceiling = juce::Decibels::decibelsToGain (p.ceilingDb);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* out = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i) out[i] = juce::jlimit (-ceiling, ceiling, out[i]);
        }
    }
}
}
