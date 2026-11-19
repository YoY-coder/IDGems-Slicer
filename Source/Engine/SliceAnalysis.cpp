#include "SliceAnalysis.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace idg
{
const juce::StringArray& featureNames()
{
    static const juce::StringArray n { "Brillance", "Longueur", "Energie", "Tonal / Bruit", "Hauteur" };
    return n;
}

namespace
{
    constexpr int fftOrder = 11, fftSize = 1 << fftOrder, hop = fftSize / 2, maxFrames = 24;

    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }

    // Estimation de hauteur (YIN simplifie) sur un signal deja sous-echantillonne
    float estimatePitch (const std::vector<float>& x, double sr)
    {
        const int w = 1024;
        const int minTau = juce::jmax (2, (int) (sr / 1000.0));
        const int maxTau = juce::jmin ((int) (sr / 40.0), (int) x.size() - w - 1);
        if (maxTau <= minTau + 2) return 0.0f;

        std::vector<float> d ((size_t) maxTau + 1, 0.0f);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float s = 0.0f;
            for (int i = 0; i < w; ++i)
            {
                const float diff = x[(size_t) i] - x[(size_t) (i + tau)];
                s += diff * diff;
            }
            d[(size_t) tau] = s;
        }

        // Difference cumulee normalisee
        float running = 0.0f;
        d[0] = 1.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            running += d[(size_t) tau];
            d[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }

        auto refine = [&] (int tau)
        {
            // interpolation parabolique
            const float a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            const float denom = a - 2.0f * b + c;
            const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
            return (float) (sr / ((float) tau + shift));
        };

        for (int tau = minTau; tau < maxTau; ++tau)
        {
            if (d[(size_t) tau] < 0.2f)
            {
                while (tau + 1 < maxTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                return refine (tau);
            }
        }

        // Repli : minimum global s'il reste assez marque
        int best = minTau;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        return d[(size_t) best] < 0.4f ? refine (best) : 0.0f;
    }
}

void analyseSlices (const AudioData& a, std::vector<Slice>& slices)
{
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> frame ((size_t) fftSize * 2);
    std::vector<float> mono;
    const double sr = a.sampleRate;
    const float binHz = (float) (sr / fftSize);

    for (auto& s : slices)
    {
        const int len = s.end - s.start;
        if (len <= 0) continue;

        // Analyse limitee aux 2 premieres secondes de la slice
        const int n = juce::jmin (len, (int) (sr * 2.0));
        mono.resize ((size_t) n);
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (a.buffer.getSample (0, s.start + i) + a.buffer.getSample (1, s.start + i));
            mono[(size_t) i] = v;
            sumSq += (double) v * v;
        }

        // Longueur : 10 ms -> 0, 10 s -> 1 (echelle log)
        const float seconds = (float) (len / sr);
        s.features[featLength] = clamp01 (std::log10 (juce::jmax (0.01f, seconds) / 0.01f) / 3.0f);

        // Energie : RMS de -60 dB a 0 dB
        const float rms = (float) std::sqrt (sumSq / juce::jmax (1, n));
        s.features[featEnergy] = clamp01 ((juce::Decibels::gainToDecibels (rms, -60.0f) + 60.0f) / 60.0f);

        // Spectre : centroide (brillance) et platitude (bruit)
        double centroidAcc = 0.0, flatAcc = 0.0, weightAcc = 0.0;
        const int frames = juce::jlimit (1, maxFrames, (n - fftSize) / hop + 1);
        for (int f = 0; f < frames; ++f)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            const int off = f * hop;
            for (int i = 0; i < fftSize && off + i < n; ++i) frame[(size_t) i] = mono[(size_t) (off + i)];
            window.multiplyWithWindowingTable (frame.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);

            double magSum = 0.0, weighted = 0.0, logSum = 0.0, powSum = 0.0;
            const int bins = fftSize / 2;
            for (int k = 1; k < bins; ++k)
            {
                const double m = frame[(size_t) k];
                magSum += m;
                weighted += m * k * binHz;
                const double p = m * m + 1.0e-12;
                logSum += std::log (p);
                powSum += p;
            }
            if (magSum < 1.0e-6) continue;
            const double centroid = weighted / magSum;
            const double flatness = std::exp (logSum / (bins - 1)) / (powSum / (bins - 1));
            centroidAcc += centroid * magSum;
            flatAcc += flatness * magSum;
            weightAcc += magSum;
        }

        if (weightAcc > 0.0)
        {
            const float centroid = (float) (centroidAcc / weightAcc);
            const float flatness = (float) (flatAcc / weightAcc);
            s.centroidHz = centroid;
            s.features[featBrightness] = clamp01 (std::log2 (juce::jmax (80.0f, centroid) / 80.0f) / std::log2 (16000.0f / 80.0f));
            s.features[featTonality] = clamp01 (1.0f - flatness / 0.35f);
        }
        else
        {
            s.features[featBrightness] = 0.0f;
            s.features[featTonality] = 0.0f;
        }

        // Hauteur : YIN sur le signal sous-echantillonne par 2, apres l'attaque
        {
            const int skip = juce::jmin (n / 3, (int) (sr * 0.03));   // on saute l'attaque (souvent bruitee)
            std::vector<float> ds;
            ds.reserve (2600);
            for (int i = skip; i + 1 < n && ds.size() < 2600; i += 2)
                ds.push_back (0.5f * (mono[(size_t) i] + mono[(size_t) i + 1]));
            s.pitchHz = ds.size() > 1100 ? estimatePitch (ds, sr * 0.5) : 0.0f;
            if (s.pitchHz > 0.0f)
            {
                const float midi = 69.0f + 12.0f * std::log2 (s.pitchHz / 440.0f);
                s.features[featPitch] = clamp01 ((midi - 24.0f) / 72.0f);   // C0..C6
            }
            else
            {
                // Pas de hauteur claire (bruit) : on place la slice selon sa brillance
                s.features[featPitch] = s.features[featBrightness];
            }
        }
    }
}

juce::String describeSlice (const Slice& s, double sr)
{
    juce::String t;
    t << juce::String ((s.end - s.start) / sr * 1000.0, 0) << " ms";
    if (s.pitchHz > 0.0f)
        t << "  |  " << juce::MidiMessage::getMidiNoteName (juce::roundToInt (69.0f + 12.0f * std::log2 (s.pitchHz / 440.0f)), true, true, 3);
    t << "  |  " << juce::roundToInt (s.centroidHz) << " Hz";
    t << "  |  " << (s.features[featTonality] > 0.5f ? "tonal" : "bruit");
    return t;
}
}
