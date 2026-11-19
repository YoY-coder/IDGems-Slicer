#include "VoiceEngine.h"

namespace idg
{
namespace
{
    constexpr double stealReleaseSeconds = 0.004;   // fondu rapide quand une voix est volee

    inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((((a * t) - b) * t + c) * t + x0);
    }

    struct Reader
    {
        const float* src[2];
        int len;

        inline float read (int ch, double pos) const noexcept
        {
            const int i0 = (int) std::floor (pos);
            const float t = (float) (pos - (double) i0);
            auto at = [this, ch] (int i) noexcept { return src[ch][i < 0 ? 0 : (i >= len ? len - 1 : i)]; };
            return hermite (at (i0 - 1), at (i0), at (i0 + 1), at (i0 + 2), t);
        }
    };
}

void VoiceEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 2 };
    for (auto& v : voices)
    {
        v.filter.prepare (spec);
        v.adsr.setSampleRate (sampleRate);
    }
    allNotesOff (true);
}

void VoiceEngine::steal (Voice& v) noexcept
{
    if (! v.active) return;
    v.stealing = true;
    v.held = false;
    v.stealStep = (float) (1.0 / juce::jmax (1.0, stealReleaseSeconds * sampleRate));
}

void VoiceEngine::release (Voice& v) noexcept
{
    if (! v.active) return;
    v.held = false;
    v.adsr.noteOff();
}

void VoiceEngine::stopVoice (Voice& v) noexcept
{
    v.active = v.held = v.stealing = false;
    v.data = nullptr;   // le release pool garde une reference : pas de liberation ici
    v.note = -1;
    v.slot = -1;
    v.adsr.reset();
}

void VoiceEngine::noteOn (const SampleData::Ptr& data, const NoteParams& p) noexcept
{
    if (data == nullptr || data->slices.empty()) return;

    // Limite de polyphonie : fondu rapide des plus anciennes voix
    const int poly = juce::jlimit (1, 16, p.polyphony);
    for (;;)
    {
        int playing = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.stealing)
            {
                ++playing;
                if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            }
        if (playing < poly || oldest == nullptr) break;
        steal (*oldest);
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (target == nullptr || v.age < target->age) target = &v;

    const auto& slice = data->slices[(size_t) juce::jlimit (0, data->numSlices() - 1, p.slice)];
    const int sliceLen = slice.end - slice.start;
    const int playLen = juce::jmax (32, (int) (sliceLen * juce::jlimit (0.02f, 1.0f, p.lengthFraction)));

    auto& v = *target;
    v.data = data;
    v.active = true;
    v.held = ! p.oneShot;
    v.stealing = false;
    v.steal = 1.0f;
    v.slot = p.slot;
    v.note = p.note;
    v.sliceStart = slice.start;
    v.sliceEnd = slice.end;
    v.reverse = p.reverse;
    v.start = p.reverse ? slice.end - playLen : slice.start;
    v.end = p.reverse ? slice.end : slice.start + playLen;

    const double offset = (double) juce::jlimit (0.0f, 0.95f, p.startFraction) * playLen;
    v.pos = p.reverse ? (double) v.end - 1.0 - offset : (double) v.start + offset;
    v.startPos = v.pos;
    v.inc = juce::jlimit (0.01, 16.0, p.pitchRatio);
    v.gateRemaining = p.gateSamples;
    v.gain = p.velocity * (p.normalise ? slice.normGain : 1.0f);
    v.fadeLen = (float) juce::jmin (p.fadeSeconds * data->audio->sampleRate, (double) playLen * 0.5);

    v.adsr.setParameters (p.adsr);
    v.adsr.reset();
    v.adsr.noteOn();

    v.clickSemis = p.clickSemitones;
    v.clickEnv = 1.0f;
    v.clickDecay = (float) std::exp (-1.0 / juce::jmax (1.0, p.clickSeconds * sampleRate));

    v.freeze = p.freeze;
    v.fxSend = juce::jlimit (0.0f, 1.0f, p.fxSend);
    v.grainPhase = 0.0;
    v.played = 0.0f;
    // Un freeze en one-shot dure la longueur (jouee) de la slice
    v.oneShotRemaining = (p.freeze && p.oneShot) ? (juce::int64) (playLen / v.inc) : -1;

    v.filter.reset();
    v.filterTypeSet = -1;
    v.age = ++ageCounter;
}

void VoiceEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
            release (v);
}

void VoiceEngine::allNotesOff (bool immediate) noexcept
{
    for (auto& v : voices)
    {
        if (immediate) stopVoice (v);
        else           release (v);
    }
}

void VoiceEngine::render (juce::AudioBuffer<float>& out, juce::AudioBuffer<float>& fx, int start, int num, const LiveParams& lp) noexcept
{
    const int outCh = out.getNumChannels();
    const bool hasFx = fx.getNumChannels() >= outCh && fx.getNumSamples() >= start + num;
    const float driveGain = 1.0f + lp.drive * 24.0f;
    const float driveComp = 1.0f / (1.0f + lp.drive * 3.0f);

    for (size_t vi = 0; vi < voices.size(); ++vi)
    {
        auto& v = voices[vi];
        if (! v.active)
        {
            display[vi].slot.store (-1, std::memory_order_relaxed);
            continue;
        }

        const auto& buf = v.data->audio->buffer;
        const Reader rd { { buf.getReadPointer (0), buf.getReadPointer (1) }, buf.getNumSamples() };
        const double fileRate = v.data->audio->sampleRate;

        // Filtre (type et coefficients mis a jour a chaque bloc)
        const bool useFilter = lp.filterType > 0;
        if (useFilter)
        {
            if (v.filterTypeSet != lp.filterType)
            {
                using T = juce::dsp::StateVariableTPTFilterType;
                v.filter.setType (lp.filterType == 1 ? T::lowpass : lp.filterType == 2 ? T::highpass : T::bandpass);
                v.filterTypeSet = lp.filterType;
            }
            v.filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRate * 0.45), lp.cutoff));
            v.filter.setResonance (juce::jmax (0.5f, lp.resonance));
        }

        // Grain du freeze
        const int sliceLen = v.sliceEnd - v.sliceStart;
        const double grainLen = juce::jlimit (32.0, (double) juce::jmax (64, sliceLen), lp.freezeSeconds * fileRate);
        const double grainStart = v.sliceStart + juce::jlimit (0.0, 1.0, (double) lp.freezePosition) * juce::jmax (0.0, sliceLen - grainLen);
        const double xfade = juce::jmax (8.0, grainLen * 0.5 * juce::jlimit (0.0f, 1.0f, lp.freezeFade));

        for (int n = 0; n < num; ++n)
        {
            const bool endReached = ! v.freeze && (v.pos >= (double) v.end || v.pos < (double) v.start);
            if (endReached || ! v.adsr.isActive() || v.steal <= 0.0f || v.oneShotRemaining == 0)
            {
                stopVoice (v);
                break;
            }

            // Enveloppe : fondus anti-clic, ADSR, vol de voix
            float env = v.adsr.getNextSample();
            if (v.fadeLen > 1.0f)
            {
                float e = v.played / v.fadeLen;
                if (! v.freeze)
                {
                    const float toEnd = (float) (v.reverse ? v.pos - (double) v.start : (double) v.end - v.pos);
                    e = juce::jmin (e, toEnd / v.fadeLen);
                }
                env *= juce::jmin (1.0f, e);
            }
            if (v.stealing) { env *= v.steal; v.steal -= v.stealStep; }
            if (v.gateRemaining > 0 && --v.gateRemaining == 0) release (v);
            if (v.oneShotRemaining > 0) --v.oneShotRemaining;

            // Pitch "clic" : la lecture demarre plus vite puis revient a la normale
            double inc = v.inc;
            if (v.clickSemis != 0.0f && v.clickEnv > 1.0e-4f)
            {
                inc *= std::exp2 ((double) (v.clickSemis * v.clickEnv) / 12.0);
                v.clickEnv *= v.clickDecay;
            }

            const float g = v.gain * env;
            for (int ch = 0; ch < outCh; ++ch)
            {
                const int c = ch < 2 ? ch : 1;
                float smp;
                if (v.freeze)
                {
                    // Boucle du grain avec fondu enchaine en fin de boucle
                    const double p = v.grainPhase;
                    smp = rd.read (c, grainStart + p);
                    if (p > grainLen - xfade)
                    {
                        const float t = (float) ((p - (grainLen - xfade)) / xfade);
                        smp = smp * std::sqrt (1.0f - t) + rd.read (c, grainStart + p - grainLen) * std::sqrt (t);
                    }
                }
                else
                {
                    smp = rd.read (c, v.pos);
                }

                if (useFilter)
                {
                    if (lp.drive > 0.0f) smp = std::tanh (smp * driveGain) * driveComp;
                    smp = v.filter.processSample (c, smp);
                }
                else if (lp.drive > 0.0f)
                {
                    smp = std::tanh (smp * driveGain) * driveComp;
                }

                const float y = smp * g;
                if (hasFx && v.fxSend > 0.0f)
                {
                    out.addSample (ch, start + n, y * (1.0f - v.fxSend));
                    fx.addSample (ch, start + n, y * v.fxSend);
                }
                else
                {
                    out.addSample (ch, start + n, y);
                }
            }

            if (v.played < 1.0e6f) v.played += (float) inc;
            if (v.freeze)
            {
                v.grainPhase += inc;
                if (v.grainPhase >= grainLen) v.grainPhase -= grainLen;
                v.pos = grainStart + v.grainPhase;
            }
            else
            {
                v.pos += v.reverse ? -inc : inc;
            }
        }

        if (v.active)
        {
            display[vi].slot.store (v.slot, std::memory_order_relaxed);
            display[vi].position.store ((float) (v.pos / (double) juce::jmax (1, rd.len)), std::memory_order_relaxed);
        }
        else
        {
            display[vi].slot.store (-1, std::memory_order_relaxed);
        }
    }
}
}
