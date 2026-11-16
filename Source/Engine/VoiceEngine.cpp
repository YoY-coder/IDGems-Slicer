#include "VoiceEngine.h"

namespace idg
{
namespace
{
    constexpr double gateReleaseSeconds  = 0.030;   // relachement de note (sera l'ADSR a l'etape 6)
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
}

void VoiceEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    allNotesOff (true);
}

void VoiceEngine::startRelease (Voice& v, double seconds) noexcept
{
    if (! v.active) return;
    const float steps = (float) juce::jmax (1.0, seconds * sampleRate);
    const float step = v.release / steps;
    if (! v.releasing || step > v.releaseStep)
        v.releaseStep = step;
    v.releasing = true;
    v.held = false;
}

void VoiceEngine::stopVoice (Voice& v) noexcept
{
    v.active = v.held = v.releasing = false;
    v.data = nullptr;   // le release pool garde une reference : pas de liberation ici
    v.note = -1;
    v.slot = -1;
}

void VoiceEngine::noteOn (const SampleData::Ptr& data, const NoteParams& p) noexcept
{
    if (data == nullptr || data->slices.empty()) return;

    // Limite de polyphonie : on relache rapidement les plus anciennes voix
    const int poly = juce::jlimit (1, 16, p.polyphony);
    for (;;)
    {
        int playing = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.releasing)
            {
                ++playing;
                if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            }
        if (playing < poly || oldest == nullptr) break;
        startRelease (*oldest, stealReleaseSeconds);
    }

    // Voix libre, sinon la plus ancienne en relachement
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (target == nullptr || v.age < target->age) target = &v;

    const auto& slice = data->slices[(size_t) juce::jlimit (0, data->numSlices() - 1, p.slice)];
    auto& v = *target;
    v.data = data;
    v.active = true;
    v.held = ! p.oneShot;
    v.releasing = false;
    v.slot = p.slot;
    v.note = p.note;
    v.start = slice.start;
    v.end = slice.end;
    v.pos = (double) slice.start;
    v.inc = juce::jlimit (0.01, 16.0, p.pitchRatio);
    v.gain = p.velocity * (p.normalise ? slice.normGain : 1.0f);
    v.fadeLen = (float) juce::jmin (p.fadeSeconds * data->audio->sampleRate, (double) (slice.end - slice.start) * 0.5);
    v.release = 1.0f;
    v.releaseStep = 0.0f;
    v.age = ++ageCounter;
}

void VoiceEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
            startRelease (v, gateReleaseSeconds);
}

void VoiceEngine::allNotesOff (bool immediate) noexcept
{
    for (auto& v : voices)
    {
        if (immediate) stopVoice (v);
        else           startRelease (v, gateReleaseSeconds);
    }
}

void VoiceEngine::render (juce::AudioBuffer<float>& out, int start, int num) noexcept
{
    const int outCh = out.getNumChannels();

    for (size_t vi = 0; vi < voices.size(); ++vi)
    {
        auto& v = voices[vi];
        if (! v.active)
        {
            display[vi].slot.store (-1, std::memory_order_relaxed);
            continue;
        }

        const auto& buf = v.data->audio->buffer;
        const int len = buf.getNumSamples();
        const float* src[2] = { buf.getReadPointer (0), buf.getReadPointer (1) };
        auto clampIdx = [len] (int i) noexcept { return i < 0 ? 0 : (i >= len ? len - 1 : i); };

        for (int n = 0; n < num; ++n)
        {
            if (v.pos >= (double) v.end || v.release <= 0.0f)
            {
                stopVoice (v);
                break;
            }

            const int i0 = (int) v.pos;
            const float t = (float) (v.pos - (double) i0);

            float env = 1.0f;
            if (v.fadeLen > 1.0f)
            {
                const float fromStart = (float) (v.pos - (double) v.start);
                const float toEnd = (float) ((double) v.end - v.pos);
                env = juce::jmin (1.0f, fromStart / v.fadeLen, toEnd / v.fadeLen);
            }
            if (v.releasing)
            {
                env *= v.release;
                v.release -= v.releaseStep;
            }

            const float g = v.gain * env;
            for (int ch = 0; ch < outCh; ++ch)
            {
                const float* s = src[ch < 2 ? ch : 1];
                const float smp = hermite (s[clampIdx (i0 - 1)], s[clampIdx (i0)],
                                           s[clampIdx (i0 + 1)], s[clampIdx (i0 + 2)], t);
                out.addSample (ch, start + n, smp * g);
            }
            v.pos += v.inc;
        }

        if (v.active)
        {
            display[vi].slot.store (v.slot, std::memory_order_relaxed);
            display[vi].position.store ((float) (v.pos / (double) juce::jmax (1, len)), std::memory_order_relaxed);
        }
        else
        {
            display[vi].slot.store (-1, std::memory_order_relaxed);
        }
    }
}
}
