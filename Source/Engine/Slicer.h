#pragma once

#include "SampleData.h"

namespace idg
{
    enum class SliceMode { transients = 0, gridTempo, equalParts, random, manual };

    struct SliceSettings
    {
        SliceMode mode = SliceMode::transients;
        int   count = 32;              // nombre de slices vise (16..256)
        float sensitivity = 0.5f;      // transitoires (0..1)
        int   gridDivIndex = 2;        // 0:1/4 1:1/8 2:1/16 3:1/32 4:1/64
        int   seed = 1;                // aleatoire

        bool operator== (const SliceSettings& o) const
        {
            return mode == o.mode && count == o.count && juce::approximatelyEqual (sensitivity, o.sensitivity)
                && gridDivIndex == o.gridDivIndex && seed == o.seed;
        }
    };

    const juce::StringArray& sliceModeNames();
    const juce::StringArray& gridDivisionNames();

    namespace Slicer
    {
        // Calcule l'apercu (crete par bloc) d'un buffer
        std::vector<float> computeBlockPeaks (const juce::AudioBuffer<float>&);

        // Points de coupe (positions de debut de slice) selon le mode
        std::vector<int> computeCuts (const AudioData&, const SliceSettings&, double bpm);

        // Construit les slices a partir de points de coupe (tri, nettoyage, gains)
        std::vector<Slice> buildSlices (const AudioData&, std::vector<int> cuts);
    }
}
