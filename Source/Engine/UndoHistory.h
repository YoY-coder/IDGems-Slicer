#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer.h"
#include "MapStore.h"
#include "SampleManager.h"

namespace idg
{
//==============================================================================
// Undo / redo global (thread message). Un instantane de tout l'etat
// (parametres, banque de patterns, trajet, decoupes, fichiers) est memorise
// des que l'etat se stabilise apres une modification.
//==============================================================================
class UndoHistory final : public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    UndoHistory (juce::AudioProcessor&, PatternStore&, MapStore&, SampleManager&);
    ~UndoHistory() override;

    bool canUndo() const noexcept { return index > 0 || pending; }
    bool canRedo() const noexcept { return index + 1 < (int) states.size() && ! pending; }
    void undo();
    void redo();
    void clear();

private:
    struct State
    {
        std::vector<float> params;
        PatternBank bank;
        int patternIndex = 0;
        MapPath path;
        std::array<SliceSettings, numSlots> slicing;
        std::array<std::vector<int>, numSlots> manualCuts;
        std::array<juce::String, numSlots> files;
        bool equals (const State&) const;
    };

    void timerCallback() override;
    State capture() const;
    void restore (const State&);

    juce::AudioProcessor& processor;
    PatternStore& patterns;
    MapStore& map;
    SampleManager& samples;

    std::vector<State> states;
    int index = -1;
    State lastSeen;
    bool hasLastSeen = false, pending = false;
    int stableTicks = 0;
};
}
