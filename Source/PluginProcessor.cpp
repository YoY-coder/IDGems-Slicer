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
        const juce::Identifier sequencer    { "Sequencer" };
    }

    constexpr int currentStateVersion = 3;
    constexpr int previewNote = -2;
    constexpr int sequencerNote = -3;
    constexpr int maxChord = 8;

    enum PlayMode { modeKeyboard = 0, modeScan, modeChord };
}

//==============================================================================
IDGemsSlicerProcessor::IDGemsSlicerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSlicerState", createParameterLayout())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    outputGainDb = p ("outputGain");
    for (int i = 0; i < idg::numSlots; ++i)
        probParams[(size_t) i] = apvts.getRawParameterValue ("prob" + juce::String (i + 1));
    noRepeatParam    = p ("noRepeat");
    keySourceParam   = p ("keySource");
    rootNoteParam    = p ("rootNote");
    polyphonyParam   = p ("polyphony");
    transposeParam   = p ("transpose");
    oneShotParam     = p ("oneShot");
    fadeParam        = p ("fadeMs");
    normalizeParam   = p ("normalize");
    playModeParam    = p ("playMode");
    sliceSelectParam = p ("sliceSelect");
    scanRandomParam  = p ("scanRandom");
    velToScanParam   = p ("velToScan");
    wheelToScanParam = p ("wheelToScan");
    pitchFollowParam = p ("pitchFollow");
    chordCountParam  = p ("chordCount");
    chordSpreadParam = p ("chordSpread");
    chordStepParam   = p ("chordStep");
    seqOnParam       = p ("seqOn");
    seqRateParam     = p ("seqRate");
    swingParam       = p ("swing");
    grooveParam      = p ("groove");

    apvts.state.setProperty (IDs::editorWidth,  baseWidth,  nullptr);
    apvts.state.setProperty (IDs::editorHeight, baseHeight, nullptr);
    apvts.state.setProperty (IDs::selectedSlot, 0, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout IDGemsSlicerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (const String& id, const String& name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outputGain", 1 }, "Output Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Aiguillage
    for (int i = 1; i <= idg::numSlots; ++i)
        layout.add (percent ("prob" + String (i), "Probabilite Sample " + String (i), 0.0f, 100.0f, 100.0f));
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

    // Modes de jeu (etape 4)
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "playMode", 1 }, "Mode de jeu", StringArray { "Clavier", "Scan", "Accord" }, 0));
    layout.add (percent ("sliceSelect", "Slice Select", 0.0f, 100.0f, 0.0f));
    layout.add (percent ("scanRandom", "Scan Random", 0.0f, 100.0f, 0.0f));
    layout.add (percent ("velToScan", "Velocite > Scan", -100.0f, 100.0f, 0.0f));
    layout.add (percent ("wheelToScan", "Molette > Scan", -100.0f, 100.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pitchFollow", 1 }, "Pitch suit la note", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "chordCount", 1 }, "Accord : slices", 2, maxChord, 3));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "chordSpread", 1 }, "Accord : spread",
        NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.5f), 60.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "chordStep", 1 }, "Accord : ecart", 1, 16, 1));

    // Sequenceur
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "seqOn", 1 }, "Sequenceur", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqRate", 1 }, "Sequenceur : resolution",
                                                        idg::seqRateNames(), 2));
    layout.add (percent ("swing", "Swing", 0.0f, 100.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "groove", 1 }, "Groove", idg::grooveNames(), 0));

    return layout;
}

//==============================================================================
void IDGemsSlicerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate);
    outputGain.reset (currentSampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    numPending = 0;
    numHeld = 0;
    stopSequencer();
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

//==============================================================================
// Declenchements
//==============================================================================
void IDGemsSlicerProcessor::schedule (juce::int64 time, const Trigger& t) noexcept
{
    if (numPending < (int) pending.size())
        pending[(size_t) numPending++] = { time, t };
}

void IDGemsSlicerProcessor::fire (const Trigger& t) noexcept
{
    const auto& data = audioSlots[(size_t) t.slot];
    if (data == nullptr || data->slices.empty()) return;

    idg::VoiceEngine::NoteParams p;
    p.slot = t.slot;
    p.slice = juce::jlimit (0, data->numSlices() - 1, t.slice);
    p.note = t.note;
    p.velocity = t.velocity;
    p.pitchRatio = data->audio->sampleRate / currentSampleRate * std::pow (2.0, (double) t.semitones / 12.0);
    p.oneShot = t.oneShot;
    p.polyphony = (int) polyphonyParam->load();
    p.fadeSeconds = fadeParam->load() * 0.001;
    p.normalise = normalizeParam->load() > 0.5f;
    p.reverse = t.reverse;
    p.startFraction = t.startFraction;
    p.gateSamples = t.gateSamples;
    engine.noteOn (data, p);

    lastTriggerSlot.store (t.slot);
    lastTriggerSlice.store (p.slice);
    triggerCounter.fetch_add (1);
}

// Un declenchement = UN sample (choisi par l'aiguillage ou force), puis une
// ou plusieurs slices de ce sample (Mode Accord), eventuellement repetees (ratchet).
void IDGemsSlicerProcessor::buildTriggers (const TriggerRequest& r, juce::int64 time, int repeats, double repeatSpacing)
{
    std::array<bool, idg::numSlots> loaded {};
    for (size_t i = 0; i < loaded.size(); ++i)
        loaded[i] = audioSlots[i] != nullptr && ! audioSlots[i]->slices.empty();

    int slot = -1;
    if (juce::isPositiveAndBelow (r.forcedSlot, idg::numSlots) && loaded[(size_t) r.forcedSlot])
        slot = r.forcedSlot;
    else
        slot = router.pick (loaded, getProbabilities(), noRepeatParam->load() > 0.5f);
    if (slot < 0) return;

    const int n = audioSlots[(size_t) slot]->numSlices();
    const int mode = (int) playModeParam->load();
    const int root = (int) rootNoteParam->load();

    int base = 0;
    if (mode == modeKeyboard)
    {
        base = (((r.note - root) % n) + n) % n;
        if (r.scanOverride >= 0.0f)
            base = (base + juce::roundToInt (r.scanOverride * (float) (n - 1))) % n;
    }
    else
    {
        float pos = r.scanOverride >= 0.0f ? r.scanOverride : sliceSelectParam->load() * 0.01f;
        pos += wheelToScanParam->load() * 0.01f * modWheel;
        pos += velToScanParam->load() * 0.01f * r.velocity;
        pos += (audioRandom.nextFloat() - 0.5f) * scanRandomParam->load() * 0.01f;
        pos -= std::floor (pos);   // reboucle dans 0..1
        base = juce::jlimit (0, n - 1, juce::roundToInt (pos * (float) (n - 1)));
    }

    Trigger t;
    t.slot = slot;
    t.note = r.voiceNote;
    t.velocity = r.velocity;
    t.semitones = transposeParam->load() + r.extraSemitones
                + ((mode != modeKeyboard && pitchFollowParam->load() > 0.5f) ? (float) (r.note - root) : 0.0f);
    t.startFraction = r.startFraction;
    t.reverse = r.reverse;
    t.oneShot = r.oneShot;
    t.gateSamples = r.gateSamples;

    const int count = mode == modeChord ? juce::jlimit (2, maxChord, (int) chordCountParam->load()) : 1;
    const int stepBetween = (int) chordStepParam->load();
    const double spread = chordSpreadParam->load() * 0.001 * currentSampleRate;

    for (int rep = 0; rep < juce::jmax (1, repeats); ++rep)
        for (int k = 0; k < count; ++k)
        {
            t.slice = (base + k * stepBetween) % n;
            schedule (time + (juce::int64) (rep * repeatSpacing + k * spread), t);
        }
}

//==============================================================================
// Sequenceur
//==============================================================================
double IDGemsSlicerProcessor::stepBeat (juce::int64 step) const noexcept
{
    const double swing = (step & 1) ? swingParam->load() * 0.01 * 0.5 : 0.0;
    const double groove = idg::grooveTiming ((int) grooveParam->load(), step);
    return seqOrigin + ((double) step + swing + groove) * seqStepBeats;
}

void IDGemsSlicerProcessor::startSequencer (int offsetInBlock)
{
    seqRunning = true;
    seqStepBeats = idg::seqRateInBeats ((int) seqRateParam->load());
    const double now = beatAt (offsetInBlock);

    if (hostPlaying)
    {
        // Cale sur la grille d'Ableton ; une note jouee juste apres un pas (30 ms) le declenche quand meme
        seqOrigin = 0.0;
        const double tolerance = 0.03 * hostBpm.load() / 60.0;
        nextStep = (juce::int64) std::ceil ((now - tolerance) / seqStepBeats);
        while (stepBeat (nextStep) < now - tolerance) ++nextStep;
    }
    else
    {
        seqOrigin = now;
        nextStep = 0;
    }
}

void IDGemsSlicerProcessor::stopSequencer()
{
    seqRunning = false;
    currentSeqStep.store (-1);
    // On annule les repetitions / accords du sequenceur encore en attente
    int j = 0;
    for (int i = 0; i < numPending; ++i)
        if (pending[(size_t) i].trigger.note != sequencerNote)
            pending[(size_t) j++] = pending[(size_t) i];
    numPending = j;
}

void IDGemsSlicerProcessor::fireSequencerStep (juce::int64 step, juce::int64 time)
{
    currentSeqStep.store (step);
    if (numHeld == 0) return;

    if (audioRandom.nextFloat() >= pattern.valueAt (idg::laneProbability, step))
        return;

    const double stepSamples = seqStepBeats / juce::jmax (1.0e-9, beatsPerSample);
    const int ratchets = juce::jlimit (1, 8, (int) pattern.valueAt (idg::laneRatchet, step));
    const double spacing = stepSamples / ratchets;

    TriggerRequest r;
    r.note = heldNotes[(size_t) numHeld - 1];
    r.velocity = heldVelocities[(size_t) numHeld - 1]
               * pattern.valueAt (idg::laneVelocity, step)
               * idg::grooveVelocity ((int) grooveParam->load(), step);
    r.forcedSlot = (int) pattern.valueAt (idg::laneSample, step) - 1;
    r.scanOverride = pattern.lanes[idg::laneSlice].enabled ? pattern.valueAt (idg::laneSlice, step) : -1.0f;
    r.extraSemitones = pattern.valueAt (idg::lanePitch, step);
    r.startFraction = pattern.valueAt (idg::laneStart, step);
    r.reverse = pattern.valueAt (idg::laneReverse, step) > 0.5f;
    r.oneShot = oneShotParam->load() > 0.5f;
    r.gateSamples = r.oneShot ? -1 : juce::jmax (64, (int) (pattern.valueAt (idg::laneGate, step) * spacing));
    r.voiceNote = sequencerNote;

    if (r.velocity > 0.0f)
        buildTriggers (r, time, ratchets, spacing);
}

void IDGemsSlicerProcessor::processSegment (juce::AudioBuffer<float>& buffer, int start, int end)
{
    if (end <= start) return;

    // Pas du sequenceur qui tombent dans ce segment
    if (seqRunning)
    {
        const double segStart = beatAt (start), segEnd = beatAt (end);
        seqStepBeats = idg::seqRateInBeats ((int) seqRateParam->load());

        // Saut de position de l'hote (boucle, deplacement) : on se recale
        if (stepBeat (nextStep) < segStart - seqStepBeats || stepBeat (nextStep) > segEnd + 4.0 * seqStepBeats)
        {
            nextStep = (juce::int64) std::ceil ((segStart - seqOrigin) / seqStepBeats);
            if (nextStep < 0) nextStep = 0;
        }

        while (stepBeat (nextStep) < segEnd)
        {
            const double b = juce::jmax (segStart, stepBeat (nextStep));
            const int offset = juce::jlimit (start, end - 1, start + (int) ((b - segStart) / juce::jmax (1.0e-12, beatsPerSample)));
            fireSequencerStep (nextStep, sampleCounter + offset);
            ++nextStep;
        }
    }

    // Rendu entre les declenchements en attente, dans l'ordre
    int cursor = start;
    const juce::int64 segEndTime = sampleCounter + end;
    for (;;)
    {
        int idx = -1;
        for (int i = 0; i < numPending; ++i)
            if (pending[(size_t) i].time < segEndTime && (idx < 0 || pending[(size_t) i].time < pending[(size_t) idx].time))
                idx = i;
        if (idx < 0) break;

        const int at = (int) juce::jlimit ((juce::int64) cursor, (juce::int64) end, pending[(size_t) idx].time - sampleCounter);
        if (at > cursor) { engine.render (buffer, cursor, at - cursor); cursor = at; }

        const auto t = pending[(size_t) idx].trigger;
        pending[(size_t) idx] = pending[(size_t) --numPending];
        fire (t);
    }
    if (cursor < end) engine.render (buffer, cursor, end - cursor);
}

//==============================================================================
// MIDI
//==============================================================================
void IDGemsSlicerProcessor::handleMidi (const juce::MidiMessage& m, int offset)
{
    const bool seqOn = seqOnParam->load() > 0.5f;

    if (m.isNoteOn())
    {
        midiActivityCounter.fetch_add (1, std::memory_order_relaxed);
        const int note = m.getNoteNumber();

        // Pile des notes tenues (la derniere est prioritaire)
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                {
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
                }
                --numHeld;
                break;
            }
        if (numHeld == (int) heldNotes.size())
        {
            for (int j = 0; j < numHeld - 1; ++j)
            {
                heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
            }
            --numHeld;
        }
        heldNotes[(size_t) numHeld] = note;
        heldVelocities[(size_t) numHeld] = m.getFloatVelocity();
        ++numHeld;

        if (seqOn)
        {
            if (! seqRunning) startSequencer (offset);
            return;
        }

        // Jeu direct : un declenchement par note
        const int source = (int) keySourceParam->load();
        TriggerRequest r;
        r.note = note;
        r.velocity = m.getFloatVelocity();
        r.forcedSlot = source - 1;
        r.oneShot = oneShotParam->load() > 0.5f;
        r.voiceNote = note;
        buildTriggers (r, sampleCounter + offset, 1, 0.0);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        for (int i = 0; i < numHeld; ++i)
            if (heldNotes[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                {
                    heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    heldVelocities[(size_t) j] = heldVelocities[(size_t) j + 1];
                }
                --numHeld;
                break;
            }

        engine.noteOff (note);
        if (numHeld == 0 && seqRunning)
            stopSequencer();
    }
    else if (m.isController() && m.getControllerNumber() == 1)
    {
        modWheel = (float) m.getControllerValue() / 127.0f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        numHeld = 0;
        numPending = 0;
        stopSequencer();
        engine.allNotesOff (m.isAllSoundOff());
    }
}

void IDGemsSlicerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    sampleManager.updateAudioCache (audioSlots);
    patternStore.pull (pattern, patternVersion);

    // Tempo et position de l'hote
    bool playing = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);
            if (pos->getIsPlaying())
                if (auto p = pos->getPpqPosition()) { playing = true; ppq = *p; }
        }

    if (seqRunning && playing != hostPlaying)
    {
        // L'hote demarre / s'arrete pendant que le sequenceur tourne : on se recale
        hostPlaying = playing;
        blockStartBeat = playing ? ppq : freeBeat;
        startSequencer (0);
    }
    hostPlaying = playing;
    beatsPerSample = hostBpm.load() / 60.0 / currentSampleRate;
    blockStartBeat = playing ? ppq : freeBeat;

    // Pre-ecoutes demandees par l'interface
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto handle = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto r = previewQueue[(size_t) (start + i)];
                if (juce::isPositiveAndBelow (r.slot, idg::numSlots))
                {
                    Trigger t;
                    t.slot = r.slot;
                    t.slice = r.slice;
                    t.note = previewNote;
                    t.velocity = 0.9f;
                    t.oneShot = true;
                    t.semitones = transposeParam->load();
                    schedule (sampleCounter, t);
                }
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // MIDI precis a l'echantillon pres
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        processSegment (buffer, pos, at);
        pos = at;
        handleMidi (meta.getMessage(), at);
    }
    processSegment (buffer, pos, numSamples);

    // Le sequenceur peut etre coupe pendant qu'il tourne
    if (seqRunning && seqOnParam->load() < 0.5f)
        stopSequencer();
    else if (! seqRunning && numHeld > 0 && seqOnParam->load() > 0.5f)
        startSequencer (numSamples - 1);

    sampleCounter += numSamples;
    freeBeat = playing ? ppq + numSamples * beatsPerSample : freeBeat + numSamples * beatsPerSample;

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    outputGain.applyGain (buffer, numSamples);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = ch < buffer.getNumChannels() ? buffer.getMagnitude (ch, 0, numSamples) : 0.0f;
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

int IDGemsSlicerProcessor::getUiProperty (const juce::Identifier& id, int def) const
{
    return (int) apvts.state.getProperty (id, def);
}

void IDGemsSlicerProcessor::setUiProperty (const juce::Identifier& id, int value)
{
    apvts.state.setProperty (id, value, nullptr);
}

//==============================================================================
void IDGemsSlicerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (IDs::stateVersion, currentStateVersion, nullptr);

    state.removeChild (state.getChildWithName (IDs::slots), nullptr);
    state.removeChild (state.getChildWithName (IDs::sequencer), nullptr);
    state.appendChild (sampleManager.toValueTree(), nullptr);
    state.appendChild (patternStore.toValueTree(), nullptr);

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
            auto seq = tree.getChildWithName (IDs::sequencer);
            tree.removeChild (slots, nullptr);
            tree.removeChild (seq, nullptr);
            apvts.replaceState (tree);
            sampleManager.fromValueTree (slots.isValid() ? slots : juce::ValueTree (IDs::slots));

            patternStore.fromValueTree (seq.isValid() ? seq : juce::ValueTree (IDs::sequencer));
        }
    }

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
