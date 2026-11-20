#include "FxChain.h"

namespace idg
{
const juce::StringArray& fxModuleNames()
{
    static const juce::StringArray n { "BITCRUSH", "FILTRE", "TAPE STOP", "STUTTER", "GRANULAIRE" };
    return n;
}

int FxChain::encodeOrder (const std::array<int, numFxModules>& o)
{
    int v = 0;
    for (int i = 0; i < numFxModules; ++i) v |= (o[(size_t) i] & 15) << (i * 4);
    return v;
}

std::array<int, numFxModules> FxChain::decodeOrder (int v)
{
    std::array<int, numFxModules> o {};
    std::array<bool, numFxModules> used {};
    bool valid = true;
    for (int i = 0; i < numFxModules; ++i)
    {
        o[(size_t) i] = (v >> (i * 4)) & 15;
        if (o[(size_t) i] >= numFxModules || used[(size_t) o[(size_t) i]]) valid = false;
        else used[(size_t) o[(size_t) i]] = true;
    }
    if (! valid) o = { 0, 1, 2, 3, 4 };
    return o;
}

void FxChain::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    scratch.setSize (2, juce::jmax (512, maxBlockSize));
    filter.prepare ({ sampleRate, (juce::uint32) juce::jmax (512, maxBlockSize), 2 });
    tapeRing.prepare ((int) (sampleRate * 4.0));
    stutterRing.prepare ((int) (sampleRate * 4.5));
    grainRing.prepare ((int) (sampleRate * 2.5));
    reset();
}

void FxChain::reset()
{
    crushHold[0] = crushHold[1] = 0.0f;
    crushCounter = 0.0f;
    filter.reset();
    filterTypeSet = -1;
    tapeRead = 0.0; tapeSpeed = 1.0; tapeWasEngaged = false; tapeReturn = 1.0f;
    stutterOn = false; lastBeatIndex = -1; autoStutter = false; stutterFade = 0.0f;
    for (auto& g : grains) g.active = false;
    grainTimer = 0.0;
    // Les tampons peuvent contenir des valeurs invalides : on les vide
    for (auto* ring : { &tapeRing, &stutterRing, &grainRing }) ring->data.clear();
}

//==============================================================================
void FxChain::processBitcrush (float* l, float* r, int n, const FxParams& p) noexcept
{
    const float levels = std::pow (2.0f, juce::jlimit (1.0f, 16.0f, p.crushBits) - 1.0f);
    const float factor = juce::jmax (1.0f, p.crushDownsample);
    for (int i = 0; i < n; ++i)
    {
        crushCounter += 1.0f;
        if (crushCounter >= factor)
        {
            crushCounter -= factor;
            crushHold[0] = std::round (l[i] * levels) / levels;
            crushHold[1] = std::round (r[i] * levels) / levels;
        }
        l[i] = crushHold[0];
        r[i] = crushHold[1];
    }
}

void FxChain::processFilter (float* l, float* r, int n, const FxParams& p) noexcept
{
    if (filterTypeSet != p.filterType)
    {
        using T = juce::dsp::StateVariableTPTFilterType;
        filter.setType (p.filterType == 0 ? T::lowpass : p.filterType == 1 ? T::highpass : T::bandpass);
        filterTypeSet = p.filterType;
    }
    filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRate * 0.45), p.filterCutoff));
    filter.setResonance (juce::jmax (0.5f, p.filterReso));
    for (int i = 0; i < n; ++i)
    {
        l[i] = filter.processSample (0, l[i]);
        r[i] = filter.processSample (1, r[i]);
    }
}

void FxChain::processTapeStop (float* l, float* r, int n, const FxParams& p) noexcept
{
    const double decel = 1.0 / juce::jmax (1.0, (double) p.tapeSeconds * sampleRate);
    const float returnStep = (float) (1.0 / (0.01 * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        tapeRing.push (l[i], r[i]);
        const double live = (double) tapeRing.write - 1.0;

        if (p.tapeEngaged && ! tapeWasEngaged)
        {
            tapeRead = live;
            tapeSpeed = 1.0;
        }
        tapeWasEngaged = p.tapeEngaged;

        if (p.tapeEngaged)
        {
            // La bande ralentit jusqu'a l'arret (courbe "moteur" : vitesse au carre)
            tapeSpeed = juce::jmax (0.0, tapeSpeed - decel);
            tapeRead += tapeSpeed * tapeSpeed;
            const float gain = (float) juce::jmin (1.0, tapeSpeed * 8.0);   // fondu en fin d'arret
            l[i] = tapeRing.read (0, tapeRead) * gain;
            r[i] = tapeRing.read (1, tapeRead) * gain;
            tapeReturn = 0.0f;
        }
        else if (tapeReturn < 1.0f)
        {
            // Retour en douceur au signal direct
            tapeReturn = juce::jmin (1.0f, tapeReturn + returnStep);
            l[i] *= tapeReturn;
            r[i] *= tapeReturn;
        }
    }
    tapeActive.store (p.tapeEngaged);
}

void FxChain::processStutter (float* l, float* r, int n, const FxParams& p, const TimeInfo& t) noexcept
{
    static const double divisions[] { 1.0, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3.0, 1.0 / 6.0 };
    const double divBeats = divisions[juce::jlimit (0, 6, p.stutterDivision)];
    const double samplesPerBeat = t.beatsPerSample > 0.0 ? 1.0 / t.beatsPerSample : sampleRate * 0.5;
    const float fadeStep = (float) (1.0 / (0.003 * sampleRate));

    for (int i = 0; i < n; ++i)
    {
        stutterRing.push (l[i], r[i]);

        // Mode auto : a chaque temps, tirage au sort (chance) d'un stutter d'un temps
        const double beat = t.blockStartBeat + i * t.beatsPerSample;
        const auto beatIndex = (juce::int64) std::floor (beat);
        if (beatIndex != lastBeatIndex)
        {
            lastBeatIndex = beatIndex;
            autoStutter = p.stutterChance > 0.0f && rng.nextFloat() < p.stutterChance;
        }

        const bool want = p.stutterEngaged || autoStutter;
        if (want && ! stutterOn)
        {
            stutterOn = true;
            stutterLen = juce::jlimit (64.0, (double) stutterRing.size - 1.0, divBeats * samplesPerBeat);
            stutterStart = (double) stutterRing.write - 1.0 - stutterLen;   // on repete le dernier fragment
            stutterPhase = 0.0;
        }

        // Fondu d'entree / de sortie du stutter ; on le libere quand le fondu de sortie est fini
        stutterFade = want ? juce::jmin (1.0f, stutterFade + fadeStep) : juce::jmax (0.0f, stutterFade - fadeStep);
        if (! want && stutterFade <= 0.0f) stutterOn = false;

        if (stutterOn && stutterFade > 0.0f)
        {
            // Mini-fondus aux bords de la boucle (anti-clic)
            const double edge = juce::jmin (stutterLen * 0.5, 0.002 * sampleRate);
            float edgeGain = 1.0f;
            if (stutterPhase < edge) edgeGain = (float) (stutterPhase / edge);
            else if (stutterPhase > stutterLen - edge) edgeGain = (float) ((stutterLen - stutterPhase) / edge);

            const float sl = stutterRing.read (0, stutterStart + stutterPhase) * edgeGain;
            const float sr = stutterRing.read (1, stutterStart + stutterPhase) * edgeGain;
            l[i] = l[i] * (1.0f - stutterFade) + sl * stutterFade;
            r[i] = r[i] * (1.0f - stutterFade) + sr * stutterFade;

            stutterPhase += 1.0;
            if (stutterPhase >= stutterLen) stutterPhase -= stutterLen;
        }
    }
    stutterActive.store (stutterOn);
}

void FxChain::processGranular (float* l, float* r, int n, const FxParams& p) noexcept
{
    const double interval = sampleRate / juce::jlimit (0.5, 100.0, (double) p.grainDensity);
    const int length = juce::jmax (64, (int) (p.grainSize * sampleRate));
    const double spray = juce::jlimit (0.0, 2.0, (double) p.grainSpray) * sampleRate;
    const float norm = 1.0f / std::sqrt (juce::jmax (1.0f, (float) length / (float) interval));

    for (int i = 0; i < n; ++i)
    {
        grainRing.push (l[i], r[i]);

        // Nouveau grain
        grainTimer -= 1.0;
        if (grainTimer <= 0.0)
        {
            grainTimer += interval * (0.5 + rng.nextDouble());
            for (auto& g : grains)
                if (! g.active)
                {
                    const double ratio = std::exp2 ((rng.nextFloat() * 2.0f - 1.0f) * p.grainPitch / 12.0f);
                    const double back = length * ratio + 64.0 + rng.nextDouble() * spray;
                    g.active = true;
                    g.pos = (double) grainRing.write - juce::jmin (back, (double) grainRing.size - 2.0);
                    g.inc = ratio;
                    g.length = length;
                    g.age = 0;
                    const float pan = rng.nextFloat();
                    g.panL = std::cos (pan * juce::MathConstants<float>::halfPi) * 1.41f;
                    g.panR = std::sin (pan * juce::MathConstants<float>::halfPi) * 1.41f;
                    break;
                }
        }

        float outL = 0.0f, outR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
            outL += grainRing.read (0, g.pos) * w * g.panL;
            outR += grainRing.read (1, g.pos) * w * g.panR;
            g.pos += g.inc;
            if (++g.age >= g.length) g.active = false;
        }
        l[i] = outL * norm;
        r[i] = outR * norm;
    }
}

//==============================================================================
void FxChain::process (juce::AudioBuffer<float>& buf, int n, const FxParams& p, const TimeInfo& t) noexcept
{
    if (n <= 0 || buf.getNumChannels() < 2) return;
    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (1);

    const auto ord = decodeOrder (order.load());
    for (int m : ord)
    {
        // Le tape stop et le stutter ecoutent en continu (tampons a jour meme eteints)
        const bool on = p.on[(size_t) m];
        if (! on)
        {
            if (m == fxTapeStop) { for (int i = 0; i < n; ++i) tapeRing.push (l[i], r[i]); tapeWasEngaged = false; tapeActive.store (false); }
            if (m == fxStutter)  { for (int i = 0; i < n; ++i) stutterRing.push (l[i], r[i]); stutterOn = false; stutterFade = 0.0f; stutterActive.store (false); }
            if (m == fxGranular) { for (int i = 0; i < n; ++i) grainRing.push (l[i], r[i]); }
            continue;
        }

        const float mix = juce::jlimit (0.0f, 1.0f, p.mix[(size_t) m]);
        const int count = juce::jmin (n, scratch.getNumSamples());
        scratch.copyFrom (0, 0, l, count);
        scratch.copyFrom (1, 0, r, count);

        switch (m)
        {
            case fxBitcrush: processBitcrush (l, r, n, p); break;
            case fxFilter:   processFilter (l, r, n, p); break;
            case fxTapeStop: processTapeStop (l, r, n, p); break;
            case fxStutter:  processStutter (l, r, n, p, t); break;
            case fxGranular: processGranular (l, r, n, p); break;
            default: break;
        }

        if (mix < 1.0f)
        {
            const float* dl = scratch.getReadPointer (0);
            const float* dr = scratch.getReadPointer (1);
            for (int i = 0; i < count; ++i)
            {
                l[i] = dl[i] + (l[i] - dl[i]) * mix;
                r[i] = dr[i] + (r[i] - dr[i]) * mix;
            }
        }
    }
}
}
