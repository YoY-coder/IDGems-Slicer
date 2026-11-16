#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace idg
{
    constexpr int numSlots      = 4;
    constexpr int minSlices     = 16;
    constexpr int maxSlices     = 256;
    constexpr int peakBlockSize = 256;    // resolution de l'apercu de forme d'onde

    //==========================================================================
    // Audio d'un fichier charge. Immuable une fois cree (partage entre threads).
    struct AudioData final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<AudioData>;

        juce::AudioBuffer<float> buffer;   // toujours 2 canaux
        double sampleRate = 44100.0;
        juce::File file;
        juce::String name;
        std::vector<float> blockPeaks;     // crete absolue par bloc de peakBlockSize
        bool truncated = false;

        int getLength() const noexcept { return buffer.getNumSamples(); }

        float peakInRange (int start, int end) const noexcept
        {
            if (blockPeaks.empty() || end <= start) return 0.0f;
            const int b0 = juce::jlimit (0, (int) blockPeaks.size() - 1, start / peakBlockSize);
            const int b1 = juce::jlimit (0, (int) blockPeaks.size() - 1, (end - 1) / peakBlockSize);
            float p = 0.0f;
            for (int b = b0; b <= b1; ++b) p = juce::jmax (p, blockPeaks[(size_t) b]);
            return p;
        }
    };

    //==========================================================================
    struct Slice
    {
        int start = 0, end = 0;     // en samples du fichier
        float normGain = 1.0f;      // gain de normalisation (applique si option active)
    };

    //==========================================================================
    // Un sample decoupe : l'audio + la liste des slices. Immuable une fois publie.
    struct SampleData final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

        AudioData::Ptr audio;
        std::vector<Slice> slices;

        int numSlices() const noexcept { return (int) slices.size(); }
    };
}
