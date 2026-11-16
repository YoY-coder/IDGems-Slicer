#include "SampleManager.h"

namespace idg
{
namespace
{
    constexpr double maxSeconds = 600.0;   // 10 minutes max par sample

    namespace IDs
    {
        const juce::Identifier slots       { "Slots" };
        const juce::Identifier slot        { "Slot" };
        const juce::Identifier index       { "index" };
        const juce::Identifier path        { "path" };
        const juce::Identifier mode        { "mode" };
        const juce::Identifier count       { "count" };
        const juce::Identifier sensitivity { "sensitivity" };
        const juce::Identifier gridDiv     { "gridDiv" };
        const juce::Identifier seed        { "seed" };
        const juce::Identifier cuts        { "cuts" };
    }

    AudioData::Ptr readAudioFile (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = file.existsAsFile() ? "Format non reconnu" : "Fichier introuvable";
            return nullptr;
        }

        auto len = reader->lengthInSamples;
        const auto maxLen = (juce::int64) (reader->sampleRate * maxSeconds);
        AudioData::Ptr a = new AudioData();
        a->truncated = len > maxLen;
        len = juce::jmin (len, maxLen);

        if (len <= 0)
        {
            error = "Fichier vide";
            return nullptr;
        }

        const int numCh = (int) juce::jlimit (1u, 2u, reader->numChannels);
        a->buffer.setSize (2, (int) len);
        a->buffer.clear();

        juce::AudioBuffer<float> tmp (numCh, (int) len);
        reader->read (&tmp, 0, (int) len, 0, true, numCh > 1);
        a->buffer.copyFrom (0, 0, tmp, 0, 0, (int) len);
        a->buffer.copyFrom (1, 0, tmp, numCh > 1 ? 1 : 0, 0, (int) len);

        a->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
        a->file = file;
        a->name = file.getFileNameWithoutExtension();
        a->blockPeaks = Slicer::computeBlockPeaks (a->buffer);
        return a;
    }

    juce::String cutsToString (const SampleData::Ptr& d)
    {
        if (d == nullptr) return {};
        juce::StringArray parts;
        for (auto& s : d->slices) parts.add (juce::String (s.start));
        return parts.joinIntoString (",");
    }

    std::vector<int> cutsFromString (const juce::String& s)
    {
        std::vector<int> cuts;
        for (auto& p : juce::StringArray::fromTokens (s, ",", ""))
            if (p.trim().isNotEmpty()) cuts.push_back (p.getIntValue());
        return cuts;
    }
}

//==============================================================================
SampleManager::SampleManager (std::function<double()> bpmFn) : getBpm (std::move (bpmFn))
{
    formats.registerBasicFormats();
    startTimer (500);
}

SampleManager::~SampleManager()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

bool SampleManager::isSupportedFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::String SampleManager::supportedWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"; }

//==============================================================================
void SampleManager::startJob (int slot, juce::File fileToLoad, std::vector<int> manualCuts, bool useManualCuts)
{
    int gen;
    AudioData::Ptr existingAudio;
    SliceSettings settings;
    {
        const juce::ScopedLock sl (lock);
        gen = ++generations[(size_t) slot];
        settings = slots[(size_t) slot].settings;
        if (fileToLoad == juce::File() && slots[(size_t) slot].data != nullptr)
            existingAudio = slots[(size_t) slot].data->audio;
        if (fileToLoad != juce::File())
            slots[(size_t) slot].loading = true;
        slicedAtBpm[(size_t) slot] = getBpm();
    }
    sendChangeMessage();

    const double bpm = slicedAtBpm[(size_t) slot];
    juce::WeakReference<SampleManager> weak (this);

    pool.addJob ([this, weak, slot, gen, fileToLoad, existingAudio, settings, bpm,
                  manualCuts = std::move (manualCuts), useManualCuts]
    {
        // Une demande plus recente existe deja : inutile de travailler
        {
            const juce::ScopedLock sl (lock);
            if (generations[(size_t) slot] != gen) return;
        }

        juce::String error;
        AudioData::Ptr audio = existingAudio;
        if (audio == nullptr)
            audio = readAudioFile (formats, fileToLoad, error);

        SampleData::Ptr result;
        if (audio != nullptr)
        {
            result = new SampleData();
            result->audio = audio;
            auto cuts = useManualCuts ? manualCuts : Slicer::computeCuts (*audio, settings, bpm);
            result->slices = Slicer::buildSlices (*audio, std::move (cuts));
        }

        juce::MessageManager::callAsync ([weak, slot, gen, result, error]
        {
            if (auto* self = weak.get())
                self->applyResult (slot, gen, result, error);
        });
    });
}

void SampleManager::applyResult (int slot, int generation, SampleData::Ptr result, juce::String error)
{
    {
        const juce::ScopedLock sl (lock);
        if (generations[(size_t) slot] != generation) return;
        auto& s = slots[(size_t) slot];
        s.loading = false;
        s.error = error;
        if (result != nullptr)
        {
            s.data = result;
            if (result->audio->truncated)
                s.error = "Tronque a 10 minutes";
        }
        else if (error.isNotEmpty())
        {
            s.data = nullptr;
        }
    }
    publish (slot, result);
    sendChangeMessage();
}

void SampleManager::publish (int slot, SampleData::Ptr d)
{
    if (d != nullptr)
        releasePool.addIfNotAlreadyThere (d);

    const juce::SpinLock::ScopedLockType sl (audioLock);
    published[(size_t) slot] = d;
}

void SampleManager::updateAudioCache (std::array<SampleData::Ptr, numSlots>& cache) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (audioLock);
    if (sl.isLocked())
        for (size_t i = 0; i < cache.size(); ++i)
            if (cache[i] != published[i])
                cache[i] = published[i];   // le release pool garde une reference : pas de liberation ici
}

void SampleManager::timerCallback()
{
    // Liberation (thread message) des donnees que plus personne n'utilise
    for (int i = releasePool.size(); --i >= 0;)
        if (releasePool.getReference (i)->getReferenceCount() == 1)
            releasePool.remove (i);

    // Mode grille tempo : re-decoupe si le tempo de l'hote change
    const double bpm = getBpm();
    for (int i = 0; i < numSlots; ++i)
    {
        bool reslice = false;
        {
            const juce::ScopedLock sl (lock);
            auto& s = slots[(size_t) i];
            reslice = s.data != nullptr && ! s.loading
                   && s.settings.mode == SliceMode::gridTempo
                   && std::abs (slicedAtBpm[(size_t) i] - bpm) > 0.01;
        }
        if (reslice) startJob (i, {}, {}, false);
    }
}

//==============================================================================
void SampleManager::loadFile (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots)) return;
    {
        const juce::ScopedLock sl (lock);
        slots[(size_t) slot].file = file;
        slots[(size_t) slot].error = {};
    }
    startJob (slot, file, {}, false);
}

void SampleManager::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots)) return;
    {
        const juce::ScopedLock sl (lock);
        ++generations[(size_t) slot];
        auto& s = slots[(size_t) slot];
        s.file = juce::File();
        s.data = nullptr;
        s.loading = false;
        s.error = {};
    }
    publish (slot, nullptr);
    sendChangeMessage();
}

void SampleManager::setSettings (int slot, const SliceSettings& newSettings)
{
    if (! juce::isPositiveAndBelow (slot, numSlots)) return;
    bool hasAudio;
    {
        const juce::ScopedLock sl (lock);
        auto& s = slots[(size_t) slot];
        if (s.settings == newSettings) return;
        s.settings = newSettings;
        hasAudio = s.data != nullptr;
    }
    if (newSettings.mode != SliceMode::manual && hasAudio)
        startJob (slot, {}, {}, false);
    else
        sendChangeMessage();
}

void SampleManager::rerollRandom (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots)) return;
    SliceSettings s;
    {
        const juce::ScopedLock sl (lock);
        s = slots[(size_t) slot].settings;
    }
    s.mode = SliceMode::random;
    s.seed = juce::Random::getSystemRandom().nextInt (1 << 30) + 1;
    setSettings (slot, s);
}

void SampleManager::setManualCuts (int slot, std::vector<int> cuts)
{
    if (! juce::isPositiveAndBelow (slot, numSlots)) return;
    SampleData::Ptr result;
    int gen;
    {
        const juce::ScopedLock sl (lock);
        auto& s = slots[(size_t) slot];
        if (s.data == nullptr) return;
        s.settings.mode = SliceMode::manual;
        result = new SampleData();
        result->audio = s.data->audio;
        result->slices = Slicer::buildSlices (*result->audio, std::move (cuts));  // rapide (apercu precalcule)
        gen = ++generations[(size_t) slot];
    }
    applyResult (slot, gen, result, {});
}

SampleManager::SlotInfo SampleManager::getSlot (int slot) const
{
    const juce::ScopedLock sl (lock);
    return slots[(size_t) juce::jlimit (0, numSlots - 1, slot)];
}

bool SampleManager::isLoaded (int slot) const
{
    const juce::ScopedLock sl (lock);
    return slots[(size_t) juce::jlimit (0, numSlots - 1, slot)].data != nullptr;
}

//==============================================================================
juce::ValueTree SampleManager::toValueTree() const
{
    juce::ValueTree tree (IDs::slots);
    const juce::ScopedLock sl (lock);
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        juce::ValueTree t (IDs::slot);
        t.setProperty (IDs::index, i, nullptr);
        t.setProperty (IDs::path, s.file.getFullPathName(), nullptr);
        t.setProperty (IDs::mode, (int) s.settings.mode, nullptr);
        t.setProperty (IDs::count, s.settings.count, nullptr);
        t.setProperty (IDs::sensitivity, s.settings.sensitivity, nullptr);
        t.setProperty (IDs::gridDiv, s.settings.gridDivIndex, nullptr);
        t.setProperty (IDs::seed, s.settings.seed, nullptr);
        t.setProperty (IDs::cuts, cutsToString (s.data), nullptr);
        tree.appendChild (t, nullptr);
    }
    return tree;
}

void SampleManager::fromValueTree (const juce::ValueTree& tree)
{
    // Peut etre appele hors du thread message : on renvoie le travail sur celui-ci
    juce::WeakReference<SampleManager> weak (this);
    juce::MessageManager::callAsync ([weak, tree]
    {
        auto* self = weak.get();
        if (self == nullptr) return;

        for (int i = 0; i < numSlots; ++i)
        {
            auto t = tree.getChildWithProperty (IDs::index, i);
            if (! t.isValid())
            {
                self->clearSlot (i);
                continue;
            }

            SliceSettings s;
            s.mode = (SliceMode) juce::jlimit (0, 4, (int) t.getProperty (IDs::mode, 0));
            s.count = juce::jlimit (minSlices, maxSlices, (int) t.getProperty (IDs::count, 32));
            s.sensitivity = (float) (double) t.getProperty (IDs::sensitivity, 0.5);
            s.gridDivIndex = juce::jlimit (0, 4, (int) t.getProperty (IDs::gridDiv, 2));
            s.seed = (int) t.getProperty (IDs::seed, 1);

            const juce::String path = t.getProperty (IDs::path).toString();
            const auto cuts = cutsFromString (t.getProperty (IDs::cuts).toString());

            {
                const juce::ScopedLock sl (self->lock);
                auto& slot = self->slots[(size_t) i];
                slot.settings = s;
                slot.file = path.isNotEmpty() ? juce::File (path) : juce::File();
                slot.data = nullptr;
                slot.error = {};
            }
            self->publish (i, nullptr);

            if (path.isEmpty())
            {
                self->clearSlot (i);
                continue;
            }
            // On reprend exactement les slices sauvegardees
            self->startJob (i, juce::File (path), cuts, ! cuts.empty());
        }
        self->sendChangeMessage();
    });
}
}
