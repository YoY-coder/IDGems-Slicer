#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    namespace IDs
    {
        const juce::Identifier editorWidth  { "editorWidth" };
        const juce::Identifier editorHeight { "editorHeight" };
        const juce::Identifier stateVersion { "stateVersion" };
        const juce::Identifier selectedSlot { "selectedSlot" };
        const juce::Identifier slots        { "Slots" };
    }

    constexpr int currentStateVersion = 2;
    constexpr int previewNote = -2;
}

//==============================================================================
IDGemsSlicerProcessor::IDGemsSlicerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSlicerState", createParameterLayout())
{
    outputGainDb = apvts.getRawParameterValue ("outputGain");
    for (int i = 0; i < idg::numSlots; ++i)
        probParams[(size_t) i] = apvts.getRawParameterValue ("prob" + juce::String (i + 1));
    noRepeatParam  = apvts.getRawParameterValue ("noRepeat");
    keySourceParam = apvts.getRawParameterValue ("keySource");
    rootNoteParam  = apvts.getRawParameterValue ("rootNote");
    polyphonyParam = apvts.getRawParameterValue ("polyphony");
    transposeParam = apvts.getRawParameterValue ("transpose");
    oneShotParam   = apvts.getRawParameterValue ("oneShot");
    fadeParam      = apvts.getRawParameterValue ("fadeMs");
    normalizeParam = apvts.getRawParameterValue ("normalize");

    apvts.state.setProperty (IDs::editorWidth,  baseWidth,  nullptr);
    apvts.state.setProperty (IDs::editorHeight, baseHeight, nullptr);
    apvts.state.setProperty (IDs::selectedSlot, 0, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout IDGemsSlicerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outputGain", 1 }, "Output Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Aiguillage
    for (int i = 1; i <= idg::numSlots; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "prob" + String (i), 1 }, "Probabilite Sample " + String (i),
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "noRepeat", 1 }, "Pas de repetition", false));

    // Jeu
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "keySource", 1 }, "Source Clavier",
        StringArray { "Auto", "Sample 1", "Sample 2", "Sample 3", "Sample 4" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "rootNote", 1 }, "Note racine", 0, 127, 36));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "polyphony", 1 }, "Polyphonie", 1, 16, 8));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "transpose", 1 }, "Transposition",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "oneShot", 1 }, "One-shot", false));

    // Decoupe (global)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fadeMs", 1 }, "Anti-clic",
        NormalisableRange<float> (0.0f, 50.0f, 0.1f, 0.5f), 3.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "normalize", 1 }, "Normaliser les slices", false));

    return layout;
}

//==============================================================================
void IDGemsSlicerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate);
    outputGain.reset (currentSampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
}

bool IDGemsSlicerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

std::array<float, idg::numSlots> IDGemsSlicerProcessor::getProbabilities() const noexcept
{
    std::array<float, idg::numSlots> p {};
    for (size_t i = 0; i < p.size(); ++i) p[i] = probParams[i]->load() * 0.01f;
    return p;
}

void IDGemsSlicerProcessor::requestPreview (int slot, int slice)
{
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0) previewQueue[(size_t) scope.startIndex1] = { slot, slice };
    else if (scope.blockSize2 > 0) previewQueue[(size_t) scope.startIndex2] = { slot, slice };
}

void IDGemsSlicerProcessor::triggerSlice (int slot, int slice, int note, float velocity, bool forceOneShot)
{
    const auto& data = audioSlots[(size_t) slot];
    if (data == nullptr || data->slices.empty()) return;

    idg::VoiceEngine::NoteParams p;
    p.slot = slot;
    p.slice = slice;
    p.note = note;
    p.velocity = velocity;
    p.pitchRatio = data->audio->sampleRate / currentSampleRate
                   * std::pow (2.0, (double) transposeParam->load() / 12.0);
    p.oneShot = forceOneShot || oneShotParam->load() > 0.5f;
    p.polyphony = (int) polyphonyParam->load();
    p.fadeSeconds = fadeParam->load() * 0.001;
    p.normalise = normalizeParam->load() > 0.5f;
    engine.noteOn (data, p);

    lastTriggerSlot.store (slot);
    lastTriggerSlice.store (slice);
    triggerCounter.fetch_add (1);
}

void IDGemsSlicerProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        midiActivityCounter.fetch_add (1, std::memory_order_relaxed);

        std::array<bool, idg::numSlots> loaded {};
        for (size_t i = 0; i < loaded.size(); ++i)
            loaded[i] = audioSlots[i] != nullptr && ! audioSlots[i]->slices.empty();

        // Source : Auto = aiguillage par probabilite, sinon sample force
        int slot = -1;
        const int source = (int) keySourceParam->load();
        if (source > 0 && loaded[(size_t) (source - 1)])
            slot = source - 1;
        else
            slot = router.pick (loaded, getProbabilities(), noRepeatParam->load() > 0.5f);

        if (slot < 0) return;

        // Mode Clavier : une slice par touche a partir de la note racine
        const int n = audioSlots[(size_t) slot]->numSlices();
        const int rel = m.getNoteNumber() - (int) rootNoteParam->load();
        const int slice = ((rel % n) + n) % n;
        triggerSlice (slot, slice, m.getNoteNumber(), m.getFloatVelocity(), false);
    }
    else if (m.isNoteOff())
    {
        engine.noteOff (m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        engine.allNotesOff (m.isAllSoundOff());
    }
}

void IDGemsSlicerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    sampleManager.updateAudioCache (audioSlots);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    // Pre-ecoutes demandees par l'interface
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto handle = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto r = previewQueue[(size_t) (start + i)];
                if (juce::isPositiveAndBelow (r.slot, idg::numSlots))
                    triggerSlice (r.slot, r.slice, previewNote, 0.9f, true);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // MIDI precis a l'echantillon pres
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (at > pos) engine.render (buffer, pos, at - pos);
        pos = at;
        handleMidi (meta.getMessage());
    }
    if (pos < buffer.getNumSamples())
        engine.render (buffer, pos, buffer.getNumSamples() - pos);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = ch < buffer.getNumChannels() ? buffer.getMagnitude (ch, 0, buffer.getNumSamples()) : 0.0f;
        const float prev = outputLevel[(size_t) ch].load (std::memory_order_relaxed);
        outputLevel[(size_t) ch].store (juce::jmax (peak, prev * 0.9f), std::memory_order_relaxed);
    }
}

//==============================================================================
juce::AudioProcessorEditor* IDGemsSlicerProcessor::createEditor()
{
    return new IDGemsSlicerEditor (*this);
}

juce::Point<int> IDGemsSlicerProcessor::getSavedEditorSize() const
{
    return { (int) apvts.state.getProperty (IDs::editorWidth,  baseWidth),
             (int) apvts.state.getProperty (IDs::editorHeight, baseHeight) };
}

void IDGemsSlicerProcessor::setSavedEditorSize (int w, int h)
{
    apvts.state.setProperty (IDs::editorWidth,  w, nullptr);
    apvts.state.setProperty (IDs::editorHeight, h, nullptr);
}

int IDGemsSlicerProcessor::getSelectedSlot() const
{
    return juce::jlimit (0, idg::numSlots - 1, (int) apvts.state.getProperty (IDs::selectedSlot, 0));
}

void IDGemsSlicerProcessor::setSelectedSlot (int s)
{
    apvts.state.setProperty (IDs::selectedSlot, juce::jlimit (0, idg::numSlots - 1, s), nullptr);
}

//==============================================================================
void IDGemsSlicerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (IDs::stateVersion, currentStateVersion, nullptr);

    state.removeChild (state.getChildWithName (IDs::slots), nullptr);
    state.appendChild (sampleManager.toValueTree(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void IDGemsSlicerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto slots = tree.getChildWithName (IDs::slots);
            tree.removeChild (slots, nullptr);
            apvts.replaceState (tree);
            sampleManager.fromValueTree (slots.isValid() ? slots : juce::ValueTree (IDs::slots));
        }
    }

    // Si l'editeur est ouvert, il relit la taille sauvegardee
    if (auto* ed = dynamic_cast<IDGemsSlicerEditor*> (getActiveEditor()))
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<IDGemsSlicerEditor> (ed)]
        {
            if (safe != nullptr) safe->restoreSavedSize();
        });
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IDGemsSlicerProcessor();
}
