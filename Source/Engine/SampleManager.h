#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Slicer.h"
#include <array>
#include <functional>

namespace idg
{
//==============================================================================
// Gere les 4 emplacements de samples.
//  - Chargement et decoupe sur un thread d'arriere-plan (jamais dans l'audio).
//  - Publication vers le thread audio sans blocage (SpinLock en try-lock).
//  - Les objets publies sont gardes dans un "release pool" : le thread audio
//    ne libere jamais de memoire.
//==============================================================================
class SampleManager final : public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    struct SlotInfo
    {
        juce::File file;                 // fichier (meme s'il est introuvable)
        SampleData::Ptr data;            // null si vide / en chargement / erreur
        SliceSettings settings;
        bool loading = false;
        juce::String error;
    };

    explicit SampleManager (std::function<double()> getBpm);
    ~SampleManager() override;

    //== Thread message ========================================================
    static bool isSupportedFile (const juce::File&);
    static juce::String supportedWildcard();

    void loadFile (int slot, const juce::File&);
    void clearSlot (int slot);
    void setSettings (int slot, const SliceSettings&);
    void rerollRandom (int slot);
    void setManualCuts (int slot, std::vector<int> cuts);

    SlotInfo getSlot (int slot) const;
    bool isLoaded (int slot) const;

    juce::ValueTree toValueTree() const;          // appelable depuis n'importe quel thread
    void fromValueTree (const juce::ValueTree&);  // idem (les chargements partent en tache de fond)

    //== Thread audio ==========================================================
    // Copie les pointeurs publies dans le cache du thread audio (sans blocage).
    void updateAudioCache (std::array<SampleData::Ptr, numSlots>& cache) noexcept;

private:
    void timerCallback() override;
    void startJob (int slot, juce::File fileToLoad, std::vector<int> manualCuts, bool useManualCuts);
    void applyResult (int slot, int generation, SampleData::Ptr result, juce::String error);
    void publish (int slot, SampleData::Ptr);

    std::function<double()> getBpm;
    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("IDGems loader") };

    mutable juce::CriticalSection lock;
    std::array<SlotInfo, numSlots> slots;
    std::array<int, numSlots> generations {};
    std::array<double, numSlots> slicedAtBpm {};

    juce::SpinLock audioLock;
    std::array<SampleData::Ptr, numSlots> published;
    juce::Array<SampleData::Ptr> releasePool;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SampleManager)
    JUCE_DECLARE_NON_COPYABLE (SampleManager)
};
}
