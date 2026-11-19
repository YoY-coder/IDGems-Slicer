#include "Slicer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace idg
{
const juce::StringArray& sliceModeNames()
{
    static const juce::StringArray names { "Transitoires", "Grille tempo", "Parts egales", "Aleatoire", "Manuel" };
    return names;
}

const juce::StringArray& gridDivisionNames()
{
    static const juce::StringArray names { "1/4", "1/8", "1/16", "1/32", "1/64" };
    return names;
}

namespace
{
    // Mixage mono (moyenne des 2 canaux) d'un point
    inline float monoAt (const juce::AudioBuffer<float>& b, int i) noexcept
    {
        return 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    }

    // Deplace un point de coupe sur le passage par zero le plus proche (+-2 ms)
    int snapToZeroCrossing (const AudioData& a, int pos)
    {
        const int len = a.getLength();
        const int radius = juce::jmax (1, (int) (a.sampleRate * 0.002));
        if (pos <= 0 || pos >= len - 1) return juce::jlimit (0, juce::jmax (0, len - 1), pos);

        for (int d = 0; d <= radius; ++d)
        {
            for (int sgn : { -1, 1 })
            {
                const int i = pos + sgn * d;
                if (i <= 0 || i >= len - 1) continue;
                const float x0 = monoAt (a.buffer, i - 1), x1 = monoAt (a.buffer, i);
                if ((x0 <= 0.0f && x1 > 0.0f) || (x0 >= 0.0f && x1 < 0.0f))
                    return i;
            }
        }
        return pos;
    }

    //==========================================================================
    // Detection de transitoires par "flux spectral"
    std::vector<int> detectTransients (const AudioData& a, const SliceSettings& s)
    {
        constexpr int order = 10, N = 1 << order, hop = 256;
        const int len = a.getLength();
        if (len < N * 2) return { 0 };

        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);

        const int numFrames = (len - N) / hop + 1;
        std::vector<float> odf ((size_t) numFrames, 0.0f);
        std::vector<float> frame ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            const int off = f * hop;
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < N; ++i) frame[(size_t) i] = monoAt (a.buffer, off + i);
            window.multiplyWithWindowingTable (frame.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);

            float flux = 0.0f;
            for (int k = 1; k <= N / 2; ++k)
            {
                const float m = std::log1p (100.0f * frame[(size_t) k]);
                flux += juce::jmax (0.0f, m - prev[(size_t) k]);
                prev[(size_t) k] = m;
            }
            odf[(size_t) f] = f == 0 ? 0.0f : flux;
        }

        const float maxV = *std::max_element (odf.begin(), odf.end());
        if (maxV <= 0.0f) return { 0 };
        for (auto& v : odf) v /= maxV;

        // Seuil adaptatif : moyenne locale + delta (delta baisse quand la sensibilite monte)
        const float delta = juce::jmap (juce::jlimit (0.0f, 1.0f, s.sensitivity), 0.30f, 0.015f);
        const int w = 10;
        const int minGap = juce::jmax (1, (int) (0.045 * a.sampleRate / hop));

        struct Onset { int frame; float strength; };
        std::vector<Onset> onsets;

        // somme glissante pour la moyenne locale
        std::vector<double> csum ((size_t) numFrames + 1, 0.0);
        for (int f = 0; f < numFrames; ++f) csum[(size_t) f + 1] = csum[(size_t) f] + odf[(size_t) f];

        int lastFrame = -minGap * 2;
        for (int f = 1; f < numFrames - 1; ++f)
        {
            const float v = odf[(size_t) f];
            bool isPeak = true;
            for (int k = 1; k <= 3 && isPeak; ++k)
            {
                if (f - k >= 0 && odf[(size_t) (f - k)] > v) isPeak = false;
                if (f + k < numFrames && odf[(size_t) (f + k)] > v) isPeak = false;
            }
            if (! isPeak) continue;

            const int lo = juce::jmax (0, f - w), hi = juce::jmin (numFrames, f + w + 1);
            const float mean = (float) ((csum[(size_t) hi] - csum[(size_t) lo]) / (hi - lo));
            if (v < mean + delta) continue;

            if (f - lastFrame < minGap)
            {
                if (! onsets.empty() && v > onsets.back().strength)
                    onsets.back() = { f, v };
                continue;
            }
            onsets.push_back ({ f, v });
            lastFrame = f;
        }

        // Trop d'onsets : on garde les plus forts
        const int maxCuts = juce::jlimit (minSlices, maxSlices, s.count) - 1;
        if ((int) onsets.size() > maxCuts)
        {
            std::sort (onsets.begin(), onsets.end(), [] (auto& x, auto& y) { return x.strength > y.strength; });
            onsets.resize ((size_t) maxCuts);
        }

        // Position precise : dans la zone de la trame, on cherche la plus forte montee
        // d'enveloppe (blocs de 32 samples), puis on garde 1,5 ms avant l'attaque.
        std::vector<int> cuts { 0 };
        const int preRoll = (int) (a.sampleRate * 0.0015);
        constexpr int blk = 32;
        for (auto& o : onsets)
        {
            const int centre = o.frame * hop + N / 2;
            const int from = juce::jmax (0, centre - 2 * hop), to = juce::jmin (len - blk, centre + 2 * hop);
            int bestPos = juce::jmax (0, centre - hop);
            float bestRise = 0.0f, prevEnv = -1.0f;
            for (int p = from; p < to; p += blk)
            {
                float env = 0.0f;
                for (int i = 0; i < blk; ++i) env = juce::jmax (env, std::abs (monoAt (a.buffer, p + i)));
                if (prevEnv >= 0.0f && env - prevEnv > bestRise) { bestRise = env - prevEnv; bestPos = p; }
                prevEnv = env;
            }
            cuts.push_back (juce::jmax (0, bestPos - preRoll));
        }
        return cuts;
    }
}

//==============================================================================
std::vector<float> Slicer::computeBlockPeaks (const juce::AudioBuffer<float>& b)
{
    const int len = b.getNumSamples();
    std::vector<float> peaks ((size_t) ((len + peakBlockSize - 1) / peakBlockSize), 0.0f);
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const int start = (int) i * peakBlockSize;
        const int n = juce::jmin (peakBlockSize, len - start);
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = juce::jmax (p, b.getMagnitude (ch, start, n));
        peaks[i] = p;
    }
    return peaks;
}

std::vector<int> Slicer::computeCuts (const AudioData& a, const SliceSettings& s, double bpm)
{
    const int len = a.getLength();
    const int count = juce::jlimit (minSlices, maxSlices, s.count);
    std::vector<int> cuts;
    if (len <= 0) return cuts;

    switch (s.mode)
    {
        case SliceMode::transients:
            cuts = detectTransients (a, s);
            if (cuts.size() < 2)   // rien detecte : repli sur des parts egales
            {
                cuts.clear();
                for (int i = 0; i < count; ++i) cuts.push_back ((int) ((int64_t) len * i / count));
            }
            break;

        case SliceMode::gridTempo:
        {
            if (! (bpm > 20.0 && bpm < 999.0)) bpm = 120.0;
            static const double divs[] { 4.0, 8.0, 16.0, 32.0, 64.0 };
            const double div = divs[juce::jlimit (0, 4, s.gridDivIndex)];
            const double step = a.sampleRate * (60.0 / bpm) * (4.0 / div);
            for (double p = 0.0; p < len && (int) cuts.size() < maxSlices; p += step)
                cuts.push_back ((int) p);
            break;
        }

        case SliceMode::equalParts:
            for (int i = 0; i < count; ++i) cuts.push_back ((int) ((int64_t) len * i / count));
            break;

        case SliceMode::random:
        {
            juce::Random r (s.seed);
            cuts.push_back (0);
            for (int i = 1; i < count; ++i)
                cuts.push_back (1 + r.nextInt (juce::jmax (1, len - 1)));
            break;
        }

        case SliceMode::manual:
            break;
    }

    // Callage sur passage par zero (anti-clic) sauf pour la grille (on garde la precision rythmique)
    if (s.mode != SliceMode::gridTempo)
        for (auto& c : cuts)
            if (c > 0) c = snapToZeroCrossing (a, c);

    return cuts;
}

std::vector<Slice> Slicer::buildSlices (const AudioData& a, std::vector<int> cuts)
{
    const int len = a.getLength();
    std::vector<Slice> slices;
    if (len <= 0) return slices;

    const int minLen = juce::jmax (16, (int) (a.sampleRate * 0.01));   // 10 ms mini

    std::sort (cuts.begin(), cuts.end());
    std::vector<int> clean;
    for (int c : cuts)
    {
        if (c < 0 || c > len - minLen) continue;
        if (! clean.empty() && c - clean.back() < minLen) continue;
        clean.push_back (c);
    }
    if (clean.empty()) clean.push_back (0);
    if ((int) clean.size() > maxSlices) clean.resize ((size_t) maxSlices);

    slices.reserve (clean.size());
    for (size_t i = 0; i < clean.size(); ++i)
    {
        Slice s;
        s.start = clean[i];
        s.end = (i + 1 < clean.size()) ? clean[i + 1] : len;
        const float peak = a.peakInRange (s.start, s.end);
        s.normGain = peak > 1.0e-4f ? juce::jmin (16.0f, 0.89f / peak) : 1.0f;
        slices.push_back (s);
    }
    return slices;
}
}
