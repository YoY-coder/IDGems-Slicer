#pragma once

#include "SampleData.h"

namespace idg
{
    const juce::StringArray& featureNames();

    // Analyse chaque slice (brillance, longueur, energie, tonal/bruit, hauteur).
    // A appeler hors du thread audio (calcul de plusieurs dizaines de ms).
    void analyseSlices (const AudioData&, std::vector<Slice>&);

    juce::String describeSlice (const Slice&, double sampleRate);
}
