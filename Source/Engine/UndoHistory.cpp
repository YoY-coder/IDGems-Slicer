#include "UndoHistory.h"
#include <cstring>

namespace idg
{
UndoHistory::UndoHistory (juce::AudioProcessor& p, PatternStore& ps, MapStore& m, SampleManager& s)
    : processor (p), patterns (ps), map (m), samples (s)
{
    startTimer (200);
}

UndoHistory::~UndoHistory() { stopTimer(); }

bool UndoHistory::State::equals (const State& o) const
{
    return params == o.params && patternIndex == o.patternIndex
        && std::memcmp (&bank, &o.bank, sizeof (PatternBank)) == 0
        && path.count == o.path.count && std::memcmp (&path, &o.path, sizeof (MapPath)) == 0
        && slicing == o.slicing && manualCuts == o.manualCuts && files == o.files;
}

UndoHistory::State UndoHistory::capture() const
{
    State s;
    for (auto* p : processor.getParameters())
        s.params.push_back (p->getValue());
    s.bank = patterns.getBank();
    s.patternIndex = patterns.getCurrentIndex();
    s.path = map.get();
    for (int i = 0; i < numSlots; ++i)
    {
        const auto info = samples.getSlot (i);
        s.slicing[(size_t) i] = info.settings;
        s.files[(size_t) i] = info.file.getFullPathName();
        if (info.settings.mode == SliceMode::manual && info.data != nullptr)
            for (auto& sl : info.data->slices) s.manualCuts[(size_t) i].push_back (sl.start);
    }
    return s;
}

void UndoHistory::restore (const State& s)
{
    const auto& params = processor.getParameters();
    for (int i = 0; i < params.size() && i < (int) s.params.size(); ++i)
        if (std::abs (params[i]->getValue() - s.params[(size_t) i]) > 1.0e-6f)
        {
            params[i]->beginChangeGesture();
            params[i]->setValueNotifyingHost (s.params[(size_t) i]);
            params[i]->endChangeGesture();
        }

    patterns.setBank (s.bank);
    patterns.selectPattern (s.patternIndex);
    map.setPath (s.path);

    for (int i = 0; i < numSlots; ++i)
    {
        const auto info = samples.getSlot (i);
        if (info.file.getFullPathName() != s.files[(size_t) i])
        {
            if (s.files[(size_t) i].isEmpty()) samples.clearSlot (i);
            else samples.loadFile (i, juce::File (s.files[(size_t) i]));
        }
        if (s.slicing[(size_t) i].mode == SliceMode::manual && ! s.manualCuts[(size_t) i].empty())
            samples.setManualCuts (i, s.manualCuts[(size_t) i]);
        else if (! (info.settings == s.slicing[(size_t) i]))
            samples.setSettings (i, s.slicing[(size_t) i]);
    }
}

void UndoHistory::timerCallback()
{
    auto now = capture();
    if (! hasLastSeen)
    {
        lastSeen = now;
        hasLastSeen = true;
        states.push_back (now);
        index = 0;
        return;
    }

    if (! now.equals (lastSeen))
    {
        lastSeen = std::move (now);
        pending = true;
        stableTicks = 0;
        sendChangeMessage();
        return;
    }

    // Etat stable depuis ~0,4 s apres une modification : on le memorise
    if (pending && ++stableTicks >= 2)
    {
        pending = false;
        if (index + 1 < (int) states.size())
            states.erase (states.begin() + index + 1, states.end());
        if (! states.empty() && states.back().equals (lastSeen)) return;
        states.push_back (lastSeen);
        if (states.size() > 100) states.erase (states.begin());
        index = (int) states.size() - 1;
        sendChangeMessage();
    }
}

void UndoHistory::undo()
{
    // La derniere retouche (meme pas encore stabilisee) devient une etape
    auto now = capture();
    if (index >= 0 && ! now.equals (states[(size_t) index]))
    {
        if (index + 1 < (int) states.size())
            states.erase (states.begin() + index + 1, states.end());
        states.push_back (now);
        index = (int) states.size() - 1;
    }
    if (index <= 0) return;
    --index;
    restore (states[(size_t) index]);
    lastSeen = states[(size_t) index];
    pending = false;
    sendChangeMessage();
}

void UndoHistory::redo()
{
    if (index + 1 >= (int) states.size()) return;
    ++index;
    restore (states[(size_t) index]);
    lastSeen = states[(size_t) index];
    pending = false;
    sendChangeMessage();
}

void UndoHistory::clear()
{
    states.clear();
    index = -1;
    hasLastSeen = false;
    pending = false;
    sendChangeMessage();
}
}
