#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace idg
{
//==============================================================================
// Enregistreur de la sortie du plugin (resample interne).
//  - Le tampon est alloue sur le thread message au moment d'armer.
//  - Le thread audio ne fait que copier des echantillons.
//  - Duree : N mesures calees sur la grille (si l'hote joue), ou jusqu'a l'arret.
//==============================================================================
class Recorder
{
public:
    enum State { idle = 0, armed, recording, finished };

    // Thread message
    void arm (int bars, double sampleRate, double bpm)
    {
        if (state.load() != idle) return;
        const double barSeconds = 4.0 * 60.0 / juce::jlimit (20.0, 999.0, bpm);
        const double seconds = bars > 0 ? bars * barSeconds + 1.0 : 180.0;   // jusqu'a l'arret : 3 min max
        buffer.setSize (2, (int) (seconds * sampleRate), false, true, false);
        buffer.clear();
        targetBars = bars;
        length.store (0);
        stopRequested.store (false);
        state.store (armed);
    }

    void requestStop() { stopRequested.store (true); }
    void cancel() { if (state.load() == armed) state.store (idle); }

    // Thread message : recupere l'enregistrement termine (puis repasse a "idle")
    bool takeRecording (juce::AudioBuffer<float>& dest)
    {
        if (state.load() != finished) return false;
        const int n = length.load();
        dest.setSize (2, juce::jmax (1, n));
        for (int ch = 0; ch < 2; ++ch) dest.copyFrom (ch, 0, buffer, ch, 0, n);
        buffer.setSize (0, 0);
        state.store (idle);
        return n > 0;
    }

    // Thread audio
    void process (const juce::AudioBuffer<float>& out, int n, double blockStartBeat, double beatsPerSample, bool hostPlaying) noexcept
    {
        int st = state.load();
        if (st == idle || st == finished) return;

        int offset = 0;
        if (st == armed)
        {
            if (stopRequested.load()) { state.store (idle); return; }
            if (hostPlaying && targetBars > 0)
            {
                // Demarrage sur la prochaine mesure
                const double nextBar = std::ceil (blockStartBeat / 4.0 - 1.0e-9) * 4.0;
                offset = (int) std::round ((nextBar - blockStartBeat) / juce::jmax (1.0e-12, beatsPerSample));
                if (offset >= n) return;
                offset = juce::jmax (0, offset);
            }
            targetLength = targetBars > 0 ? (int) std::round (targetBars * 4.0 / juce::jmax (1.0e-12, beatsPerSample))
                                          : buffer.getNumSamples();
            targetLength = juce::jmin (targetLength, buffer.getNumSamples());
            state.store (recording);
            st = recording;
        }

        const int len = length.load();
        const int count = juce::jmin (n - offset, targetLength - len);
        if (count > 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, len, out, juce::jmin (ch, out.getNumChannels() - 1), offset, count);
        length.store (len + juce::jmax (0, count));

        if (length.load() >= targetLength || stopRequested.load())
            state.store (finished);
    }

    std::atomic<int> state { idle };
    std::atomic<int> length { 0 };
    int getTargetBars() const noexcept { return targetBars; }

private:
    juce::AudioBuffer<float> buffer;
    int targetBars = 0, targetLength = 0;
    std::atomic<bool> stopRequested { false };
};
}
