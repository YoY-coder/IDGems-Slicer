#pragma once

#include "SampleData.h"
#include <array>

namespace idg
{
//==============================================================================
// Aiguillage : a chaque declenchement, tire au sort le sample (1 a 4) dans
// lequel piocher, selon les probabilites. Les emplacements vides sont ignores.
// Aucune allocation : utilisable dans le thread audio.
//==============================================================================
struct Router
{
    // Probabilites normalisees (somme = 1 sur les emplacements charges).
    static std::array<float, numSlots> normalised (const std::array<bool, numSlots>& loaded,
                                                   const std::array<float, numSlots>& probs) noexcept
    {
        std::array<float, numSlots> w {};
        float sum = 0.0f;
        int numLoaded = 0;
        for (int i = 0; i < numSlots; ++i)
        {
            w[(size_t) i] = loaded[(size_t) i] ? juce::jmax (0.0f, probs[(size_t) i]) : 0.0f;
            sum += w[(size_t) i];
            numLoaded += loaded[(size_t) i] ? 1 : 0;
        }

        if (sum <= 0.0f)   // tout a 0 : repartition egale entre les samples charges
        {
            for (int i = 0; i < numSlots; ++i) w[(size_t) i] = loaded[(size_t) i] ? 1.0f : 0.0f;
            sum = (float) numLoaded;
        }

        if (sum > 0.0f)
            for (auto& x : w) x /= sum;
        return w;
    }

    // Retourne l'index du sample choisi, ou -1 si aucun n'est charge.
    int pick (const std::array<bool, numSlots>& loaded,
              const std::array<float, numSlots>& probs,
              bool noRepeat) noexcept
    {
        auto w = normalised (loaded, probs);

        if (noRepeat && juce::isPositiveAndBelow (lastSlot, numSlots))
        {
            // On retire le dernier sample s'il reste une autre possibilite
            float others = 0.0f;
            int otherLoaded = 0;
            for (int i = 0; i < numSlots; ++i)
                if (i != lastSlot)
                {
                    others += w[(size_t) i];
                    otherLoaded += loaded[(size_t) i] ? 1 : 0;
                }

            if (others > 0.0f)
                w[(size_t) lastSlot] = 0.0f;
            else if (otherLoaded > 0)   // les autres ont une proba de 0 : on les autorise quand meme
                for (int i = 0; i < numSlots; ++i)
                    w[(size_t) i] = (i != lastSlot && loaded[(size_t) i]) ? 1.0f : 0.0f;
        }

        float sum = 0.0f;
        for (auto x : w) sum += x;
        if (sum <= 0.0f) return -1;

        float r = rng.nextFloat() * sum;
        int chosen = -1;
        for (int i = 0; i < numSlots; ++i)
        {
            if (w[(size_t) i] <= 0.0f) continue;
            chosen = i;
            if (r < w[(size_t) i]) break;
            r -= w[(size_t) i];
        }

        lastSlot = chosen;
        return chosen;
    }

    int lastSlot = -1;
    juce::Random rng;
};
}
