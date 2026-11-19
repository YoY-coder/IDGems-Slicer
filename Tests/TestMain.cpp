// =============================================================================
//  Tests automatiques du moteur IDGems Slicer (sans DAW).
//  Usage : IDGemsSlicerTests [--snapshot chemin.png]
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <algorithm>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

template <typename Fn>
auto onMessageThread (Fn&& fn)
{
    using R = decltype (fn());
    if constexpr (std::is_void_v<R>)
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread (
            [] (void* p) -> void* { (*static_cast<Fn*> (p))(); return nullptr; }, &fn);
    }
    else
    {
        R result {};
        auto wrapper = [&] { result = fn(); };
        juce::MessageManager::getInstance()->callFunctionOnMessageThread (
            [] (void* p) -> void* { (*static_cast<decltype (wrapper)*> (p))(); return nullptr; }, &wrapper);
        return result;
    }
}

// Fichier test : "batterie" synthetique, une frappe tous les 1/8 a 120 BPM
juce::File makeTestWav (const juce::String& name, double freq, int hits, double seed)
{
    const double sr = 44100.0;
    const int step = (int) (sr * 0.25);
    juce::AudioBuffer<float> b (2, step * hits);
    b.clear();
    juce::Random r ((juce::int64) seed);
    for (int h = 0; h < hits; ++h)
    {
        const double f = freq * (1.0 + 0.25 * (h % 4));
        for (int i = 0; i < step; ++i)
        {
            const double env = std::exp (-i / (sr * 0.04));
            const float s = (float) (env * (0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr)
                                            + 0.3 * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / (sr * 0.005))));
            b.setSample (0, h * step + i, s);
            b.setSample (1, h * step + i, s * 0.9f);
        }
    }
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name + ".wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                     .withNumChannels (2)
                                                                     .withBitsPerSample (24));
    if (w != nullptr)
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return file;
}

bool waitFor (std::function<bool()> cond, int timeoutMs = 10000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (onMessageThread ([&] { return cond(); })) return true;
        juce::Thread::sleep (20);
    }
    return false;
}

void setParam (IDGemsSlicerProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float renderNote (IDGemsSlicerProcessor& p, int note, int blocks = 20)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < blocks; ++i)
    {
        p.processBlock (buf, midi);
        midi.clear();
        if (i == blocks / 2) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        peak = juce::jmax (peak, buf.getMagnitude (0, 512));
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                finite = finite && std::isfinite (buf.getSample (ch, s));
    }
    return finite ? peak : -1.0f;
}

// Tient une note pendant numBlocks blocs, puis la relache ; renvoie le nombre de declenchements
int holdNote (IDGemsSlicerProcessor& p, int note, int numBlocks, bool release = true)
{
    const int before = p.triggerCounter.load();
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    for (int i = 0; i < numBlocks; ++i)
    {
        p.processBlock (buf, midi);
        midi.clear();
    }
    if (release)
    {
        midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        p.processBlock (buf, midi);
    }
    return p.triggerCounter.load() - before;
}

// Joue une note, la tient numBlocks blocs ; renvoie la crete de chaque bloc
std::vector<float> blockPeaks (IDGemsSlicerProcessor& p, int note, int numBlocks)
{
    std::vector<float> peaks;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi, none, off;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
    off.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    for (int i = 0; i < numBlocks; ++i)
    {
        p.processBlock (buf, i == 0 ? midi : none);
        float pk = buf.getMagnitude (0, 512);
        for (int s = 0; s < 512; ++s) if (! std::isfinite (buf.getSample (0, s))) pk = -1.0f;
        peaks.push_back (pk);
    }
    p.processBlock (buf, off);
    for (int i = 0; i < 40; ++i) p.processBlock (buf, none);   // laisse s'eteindre
    return peaks;
}

int lastAudibleBlock (const std::vector<float>& peaks)
{
    int last = -1;
    for (size_t i = 0; i < peaks.size(); ++i) if (peaks[i] > 0.002f) last = (int) i;
    return last;
}

void runTests (const juce::String& snapshotPath)
{
    std::cout << "== IDGems Slicer : tests ==" << std::endl;
    auto fileA = makeTestWav ("idgems_test_a", 110.0, 16, 1);
    auto fileB = makeTestWav ("idgems_test_b", 330.0, 24, 2);

    std::unique_ptr<IDGemsSlicerProcessor> proc (onMessageThread ([] { return new IDGemsSlicerProcessor(); }));
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    auto& sm = proc->getSampleManager();

    // 1. Chargement + decoupe transitoires
    onMessageThread ([&] { sm.loadFile (0, fileA); });
    check (waitFor ([&] { return sm.isLoaded (0) && ! sm.getSlot (0).loading; }), "chargement du sample 1");
    const int nA = onMessageThread ([&] { return sm.getSlot (0).data->numSlices(); });
    check (nA >= 14 && nA <= 18, "transitoires : " + juce::String (nA) + " slices (attendu ~16)");

    // 2. Parts egales
    onMessageThread ([&] { auto s = sm.getSlot (0).settings; s.mode = idg::SliceMode::equalParts; s.count = 64; sm.setSettings (0, s); });
    check (waitFor ([&] { return sm.getSlot (0).data->numSlices() == 64; }), "parts egales : 64 slices");

    // 3. Grille 1/16 a 120 BPM sur 4 s = 32 slices
    onMessageThread ([&] { auto s = sm.getSlot (0).settings; s.mode = idg::SliceMode::gridTempo; s.gridDivIndex = 2; sm.setSettings (0, s); });
    check (waitFor ([&] { return sm.getSlot (0).data->numSlices() == 32; }), "grille 1/16 @120 BPM : 32 slices");

    // 4. Aleatoire reproductible
    onMessageThread ([&] { auto s = sm.getSlot (0).settings; s.mode = idg::SliceMode::random; s.count = 48; s.seed = 42; sm.setSettings (0, s); });
    waitFor ([&] { return sm.getSlot (0).settings.mode == idg::SliceMode::random && ! sm.getSlot (0).loading
                          && sm.getSlot (0).data->numSlices() > 16; });
    const int nR = onMessageThread ([&] { return sm.getSlot (0).data->numSlices(); });
    check (nR >= 16 && nR <= 48, "aleatoire : " + juce::String (nR) + " slices");

    // 5. Manuel
    onMessageThread ([&] { sm.setManualCuts (0, { 0, 44100, 88200, 132300 }); });
    waitFor ([&] { return sm.getSlot (0).data->numSlices() == 4; });
    check (onMessageThread ([&] { return sm.getSlot (0).data->numSlices() == 4
                                       && sm.getSlot (0).settings.mode == idg::SliceMode::manual; }),
           "manuel : 4 marqueurs");

    // 6. Son : une note produit de l'audio propre
    onMessageThread ([&] { auto s = sm.getSlot (0).settings; s.mode = idg::SliceMode::transients; sm.setSettings (0, s); });
    waitFor ([&] { return sm.getSlot (0).settings.mode == idg::SliceMode::transients && ! sm.getSlot (0).loading
                          && sm.getSlot (0).data->numSlices() > 10; });
    juce::Thread::sleep (50);
    const float peak = renderNote (*proc, 36);
    check (peak > 0.05f, "une note MIDI produit du son (crete " + juce::String (peak, 3) + ")");
    check (proc->lastTriggerSlot.load() == 0 && proc->lastTriggerSlice.load() == 0, "C1 joue la slice 1");
    renderNote (*proc, 38);
    check (proc->lastTriggerSlice.load() == 2, "D1 joue la slice 3");

    // 7. Aiguillage par probabilite
    onMessageThread ([&] { sm.loadFile (1, fileB); });
    check (waitFor ([&] { return sm.isLoaded (1) && ! sm.getSlot (1).loading; }), "chargement du sample 2");
    renderNote (*proc, 36, 2);

    auto countPicks = [&] (int notes)
    {
        std::array<int, 4> counts {};
        int repeats = 0, last = -1;
        for (int i = 0; i < notes; ++i)
        {
            renderNote (*proc, 36 + (i % 12), 1);
            const int s = proc->lastTriggerSlot.load();
            if (juce::isPositiveAndBelow (s, 4)) ++counts[(size_t) s];
            if (s == last) ++repeats;
            last = s;
        }
        return std::make_pair (counts, repeats);
    };

    setParam (*proc, "prob1", 100.0f); setParam (*proc, "prob2", 0.0f);
    auto [c1, r1] = countPicks (200);
    check (c1[0] == 200, "proba 100/0 : toujours le sample 1 (" + juce::String (c1[0]) + "/200)");

    setParam (*proc, "prob1", 75.0f); setParam (*proc, "prob2", 25.0f);
    auto [c2, r2] = countPicks (2000);
    check (c2[0] > 1350 && c2[0] < 1650 && c2[2] == 0 && c2[3] == 0,
           "proba 75/25 (3 et 4 vides ignores) : " + juce::String (c2[0]) + " / " + juce::String (c2[1]));

    setParam (*proc, "noRepeat", 1.0f);
    auto [c3, r3] = countPicks (500);
    check (r3 == 0, "pas de repetition : " + juce::String (r3) + " repetitions");
    setParam (*proc, "noRepeat", 0.0f);

    setParam (*proc, "keySource", 2.0f);   // force Sample 2
    auto [c4, r4] = countPicks (100);
    check (c4[1] == 100, "source forcee Sample 2");
    setParam (*proc, "keySource", 0.0f);

    // 8. Mode Scan
    {
        setParam (*proc, "keySource", 1.0f);   // force Sample 1
        setParam (*proc, "playMode", 1.0f);
        setParam (*proc, "selectMode", 0.0f);   // Slice Select lineaire
        const int n0 = onMessageThread ([&] { return sm.getSlot (0).data->numSlices(); });
        setParam (*proc, "sliceSelect", 100.0f);
        renderNote (*proc, 72, 2);
        check (proc->lastTriggerSlice.load() == n0 - 1, "Scan 100 % : derniere slice (" + juce::String (proc->lastTriggerSlice.load() + 1) + "/" + juce::String (n0) + ")");
        setParam (*proc, "sliceSelect", 0.0f);
        renderNote (*proc, 50, 2);
        check (proc->lastTriggerSlice.load() == 0, "Scan 0 % : premiere slice, quelle que soit la note");
    }

    // 8b. Analyse des slices + carte
    {
        // Sample A : frappes de 110 / 137.5 / 165 / 192.5 Hz en boucle
        const auto d = onMessageThread ([&] { return sm.getSlot (0).data; });
        const float p0 = d->slices[0].pitchHz, p2 = d->slices[2].pitchHz;
        if (juce::SystemStats::getEnvironmentVariable ("IDG_DEBUG", {}).isNotEmpty())
            for (auto& sl : d->slices)
                std::cout << "    slice " << sl.start << "-" << sl.end << " pitch " << sl.pitchHz << " cent " << sl.centroidHz << std::endl;
        check (std::abs (p0 - 110.0f) < 4.0f && std::abs (p2 - 165.0f) < 6.0f,
               "hauteur estimee : slice 1 = " + juce::String (p0, 1) + " Hz (110), slice 3 = " + juce::String (p2, 1) + " Hz (165)");
        const auto dB = onMessageThread ([&] { return sm.getSlot (1).data; });
        check (dB->slices[0].features[idg::featBrightness] > d->slices[0].features[idg::featBrightness],
               "brillance : sample a 330 Hz plus brillant que sample a 110 Hz");

        // Carte : axe X = hauteur ; curseur pose sur la hauteur de la slice 3 -> une slice de meme note
        setParam (*proc, "selectMode", 1.0f);
        setParam (*proc, "mapAxisX", (float) idg::featPitch);
        setParam (*proc, "mapAxisY", (float) idg::featEnergy);
        setParam (*proc, "mapRadius", 0.0f);
        setParam (*proc, "mapSpread", 0.0f);
        setParam (*proc, "mapX", d->slices[2].features[idg::featPitch] * 100.0f);
        setParam (*proc, "mapY", d->slices[2].features[idg::featEnergy] * 100.0f);
        renderNote (*proc, 60, 2);
        const int picked = proc->lastTriggerSlice.load();
        check (std::abs (d->slices[(size_t) picked].pitchHz - 165.0f) < 6.0f,
               "carte : la slice jouee est la plus proche du curseur (slice " + juce::String (picked + 1) + ")");

        // Meme chose avec l'etalement (rangs) : curseur pose sur les coordonnees etalees de la slice 3
        setParam (*proc, "mapSpread", 1.0f);
        const auto spread = onMessageThread ([&] { return sm.getSpreadTable().values[0][2]; });
        setParam (*proc, "mapX", spread[idg::featPitch] * 100.0f);
        setParam (*proc, "mapY", spread[idg::featEnergy] * 100.0f);
        renderNote (*proc, 60, 2);
        const int picked2 = proc->lastTriggerSlice.load();
        check (std::abs (d->slices[(size_t) picked2].pitchHz - 165.0f) < 6.0f,
               "carte etalee : la slice jouee est la plus proche du curseur (slice " + juce::String (picked2 + 1) + ")");

        // Rayon : plus de diversite
        setParam (*proc, "mapRadius", 50.0f);
        std::set<int> seen;
        for (int i = 0; i < 60; ++i) { renderNote (*proc, 60, 1); seen.insert (proc->lastTriggerSlice.load()); }
        check (seen.size() > 4, "carte : grand rayon = slices variees (" + juce::String ((int) seen.size()) + " differentes)");
        setParam (*proc, "mapRadius", 0.0f);

        // Trajet
        idg::MapPath path;
        path.points[0] = { 0.0f, 0.0f };
        path.points[1] = { 1.0f, 1.0f };
        path.count = 2;
        const auto mid = path.at (0.5f);
        check (std::abs (mid.x - 0.5f) < 0.01f && std::abs (mid.y - 0.5f) < 0.01f, "trajet : position a mi-parcours");
        onMessageThread ([&] { proc->getMapStore().setPath (path); });
        setParam (*proc, "pathOn", 1.0f);
        renderNote (*proc, 60, 2);
        check (proc->cursorX.load() >= 0.0f && proc->cursorX.load() <= 1.0f, "trajet : le curseur suit le trace");
        setParam (*proc, "pathOn", 0.0f);
        setParam (*proc, "selectMode", 0.0f);
        setParam (*proc, "playMode", 0.0f);
        setParam (*proc, "keySource", 0.0f);
    }

    // 9. Mode Accord
    {
        setParam (*proc, "playMode", 2.0f);
        setParam (*proc, "chordCount", 3.0f);
        setParam (*proc, "chordSpread", 0.0f);
        check (holdNote (*proc, 60, 2) == 3, "Accord : 3 slices par note");
        setParam (*proc, "chordSpread", 100.0f);   // 100 ms = 4800 samples
        const int c0 = proc->triggerCounter.load();
        const int first = holdNote (*proc, 60, 1, true);     // note jouee puis relachee
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer none;
        for (int i = 0; i < 30; ++i) proc->processBlock (buf, none);
        check (first == 1 && proc->triggerCounter.load() - c0 == 3,
               "Accord spread 100 ms : 1 slice tout de suite, les 2 autres ensuite");
        setParam (*proc, "chordSpread", 60.0f);
        setParam (*proc, "playMode", 0.0f);
        setParam (*proc, "keySource", 0.0f);
    }

    // 10. Sequenceur (sans hote : tourne librement a 120 BPM)
    {
        onMessageThread ([&]
        {
            auto& ps = proc->getPatternStore();
            ps.setLength (idg::laneRatchet, 2);
            ps.setValue (idg::laneRatchet, 0, 1.0f);
            ps.setValue (idg::laneRatchet, 1, 4.0f);
            ps.setValue (idg::laneSample, 0, 2.0f);   // pas 1 : force Sample 2
            ps.setValue (idg::laneSample, 1, 1.0f);   // pas 2 : force Sample 1
        });
        setParam (*proc, "seqOn", 1.0f);
        setParam (*proc, "seqRate", 2.0f);   // 1/16 = 6000 samples a 48 kHz / 120 BPM

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer none, on, off, reset;
        reset.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        proc->processBlock (buf, reset);
        on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

        // 1er bloc : le pas 1 joue immediatement, force sur le sample 2
        const int c0 = proc->triggerCounter.load();
        proc->processBlock (buf, on);
        check (proc->triggerCounter.load() - c0 == 1 && proc->lastTriggerSlot.load() == 1,
               "sequenceur : demarre a la note, lane Sample force S2");

        // 8 pas en ~45000 samples (88 blocs) ; ratchet x1 puis x4 en alternance (lane de 2 pas) = 4*1 + 4*4 = 20
        for (int i = 1; i < 88; ++i) proc->processBlock (buf, none);
        const auto stepBeforeStop = proc->currentSeqStep.load();
        proc->processBlock (buf, off);
        const int steps = proc->triggerCounter.load() - c0;
        // pas 1..8 : x1,x4,x1,x4... = 20, moins le dernier ratchet du pas 8 qui tombe apres le relachement
        check (steps == 19, "polyrythmie : lane Ratchet de 2 pas (x1, x4) sur 8 pas = 19 declenchements ("
                            + juce::String (steps) + ", dernier pas " + juce::String (stepBeforeStop + 1) + ")");
        check (proc->currentSeqStep.load() == -1, "sequenceur : s'arrete au relachement de la note");
        for (int i = 0; i < 20; ++i) proc->processBlock (buf, none);
        check (proc->triggerCounter.load() - c0 == steps, "aucun declenchement apres l'arret");

        onMessageThread ([&] { proc->getPatternStore().setLength (idg::laneRatchet, 16); proc->getPatternStore().resetLane (idg::laneRatchet); });
        setParam (*proc, "seqOn", 0.0f);
    }

    // 10b. Reglages de voix (etape 6)
    {
        setParam (*proc, "playMode", 0.0f);
        setParam (*proc, "keySource", 1.0f);   // sample 1, slices de ~250 ms
        setParam (*proc, "seqOn", 0.0f);

        const auto ref = blockPeaks (*proc, 36, 60);
        const int refEnd = lastAudibleBlock (ref);

        setParam (*proc, "attack", 200.0f);
        const auto slow = blockPeaks (*proc, 36, 60);
        setParam (*proc, "attack", 1.0f);
        check (slow[0] < ref[0] * 0.2f, "ADSR : attaque lente = debut doux (" + juce::String (slow[0], 3) + " vs " + juce::String (ref[0], 3) + ")");

        setParam (*proc, "playLength", 50.0f);
        const int halfEnd = lastAudibleBlock (blockPeaks (*proc, 36, 60));
        setParam (*proc, "playLength", 100.0f);
        check (halfEnd > 0 && halfEnd < refEnd * 0.7, "longueur 50 % : la slice s'arrete plus tot (bloc " + juce::String (halfEnd) + " / " + juce::String (refEnd) + ")");

        setParam (*proc, "freezeOn", 1.0f);
        setParam (*proc, "freezePos", 10.0f);
        setParam (*proc, "freezeSize", 40.0f);
        const auto frozen = blockPeaks (*proc, 36, 150);
        setParam (*proc, "freezeOn", 0.0f);
        check (refEnd < 40 && frozen[140] > 0.01f,
               "Slice Freeze : le grain boucle tant que la note est tenue (crete " + juce::String (frozen[140], 3) + " apres 1,5 s)");
        check (std::all_of (frozen.begin(), frozen.end(), [] (float v) { return v >= 0.0f; }), "Slice Freeze : audio propre");

        // Filtre : un passe-haut a 3 kHz attenue fortement une frappe a 110 Hz
        setParam (*proc, "filterType", 2.0f);
        setParam (*proc, "cutoff", 3000.0f);
        const auto hp = blockPeaks (*proc, 36, 20);
        setParam (*proc, "filterType", 0.0f);
        float refSum = 0.0f, hpSum = 0.0f;
        for (int i = 2; i < 15; ++i) { refSum += ref[(size_t) i]; hpSum += hp[(size_t) i]; }
        check (hpSum < refSum * 0.3f, "filtre passe-haut 3 kHz : attenuation du grave (" + juce::String (hpSum / refSum, 2) + ")");

        setParam (*proc, "drive", 100.0f);
        setParam (*proc, "clickAmount", 36.0f);
        const auto wild = blockPeaks (*proc, 36, 30);
        setParam (*proc, "drive", 0.0f);
        setParam (*proc, "clickAmount", 0.0f);
        check (std::all_of (wild.begin(), wild.end(), [] (float v) { return v >= 0.0f && v < 4.0f; }), "drive 100 % + pitch clic : audio propre et borne");

        setParam (*proc, "reverse", 1.0f);
        const auto rev = blockPeaks (*proc, 36, 30);
        setParam (*proc, "reverse", 0.0f);
        check (rev[0] < ref[0] * 0.6f && lastAudibleBlock (rev) > 5, "reverse : la slice commence par sa queue (douce)");
        setParam (*proc, "keySource", 0.0f);
    }

    // 11. Sauvegarde / rechargement (slices identiques)
    onMessageThread ([&] { sm.setManualCuts (1, { 0, 10000, 30000, 70000, 150000 }); });
    waitFor ([&] { return sm.getSlot (1).data->numSlices() == 5; });
    juce::MemoryBlock state;
    onMessageThread ([&] { proc->getStateInformation (state); });
    std::unique_ptr<IDGemsSlicerProcessor> proc2 (onMessageThread ([] { return new IDGemsSlicerProcessor(); }));
    onMessageThread ([&] { proc2->setStateInformation (state.getData(), (int) state.getSize()); });
    auto& sm2 = proc2->getSampleManager();
    check (waitFor ([&] { return sm2.isLoaded (0) && sm2.isLoaded (1) && ! sm2.getSlot (1).loading; }), "rechargement de l'etat");
    const bool sameCuts = onMessageThread ([&]
    {
        auto a = sm.getSlot (1).data, b = sm2.getSlot (1).data;
        if (a == nullptr || b == nullptr || a->numSlices() != b->numSlices()) return false;
        for (int i = 0; i < a->numSlices(); ++i)
            if (a->slices[(size_t) i].start != b->slices[(size_t) i].start) return false;
        return true;
    });
    check (sameCuts, "slices manuelles conservees apres rechargement");
    check (onMessageThread ([&] { return sm2.getSlot (1).settings.mode == idg::SliceMode::manual; }), "mode conserve");
    check (onMessageThread ([&] { return juce::approximatelyEqual (proc2->getPatternStore().get().lanes[idg::laneSample].values[0], 2.0f); }),
           "pattern du sequenceur conserve");
    check (onMessageThread ([&] { return proc2->getMapStore().get().count == 2; }), "trajet de la carte conserve");

    // 9. Capture de l'interface
    if (snapshotPath.isNotEmpty())
    {
        proc->getSampleManager().loadFile (2, fileA);
        waitFor ([&] { return sm.isLoaded (2); });
        renderNote (*proc, 40, 3);
        onMessageThread ([&] { juce::Random r (7); proc->getPatternStore().randomiseAll (r); });
        setParam (*proc, "selectMode", 1.0f);
        setParam (*proc, "mapAxisX", 0.0f);
        setParam (*proc, "mapAxisY", 1.0f);
        for (const char* tab : { "decoupe", "sequenceur", "carte", "voix" })
        onMessageThread ([&]
        {
            proc->setUiProperty ("selectedTab", juce::String (tab) == "sequenceur" ? 1 : juce::String (tab) == "carte" ? 2 : juce::String (tab) == "voix" ? 3 : 0);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
            ed->setSize (1280, 800);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out (snapshotPath.replace (".png", juce::String ("_") + tab + ".png"));
            out.deleteFile();
            juce::FileOutputStream fos (out);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            proc->editorBeingDeleted (ed.get());
        });
        std::cout << "  captures : " << snapshotPath << " (_decoupe / _sequenceur)" << std::endl;
    }

    onMessageThread ([&] { proc.reset(); proc2.reset(); });
    std::cout << (failures == 0 ? "== TOUS LES TESTS PASSENT ==" : "== ECHECS : " + juce::String (failures) + " ==") << std::endl;
}
}

//==============================================================================
class TestApp final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override    { return "IDGemsSlicerTests"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String& args) override
    {
        juce::String snap;
        auto tokens = juce::StringArray::fromTokens (args, true);
        const int i = tokens.indexOf ("--snapshot");
        if (i >= 0 && i + 1 < tokens.size()) snap = tokens[i + 1].unquoted();

        thread = std::make_unique<std::thread> ([this, snap]
        {
            runTests (snap);
            juce::MessageManager::callAsync ([this] { setApplicationReturnValue (failures == 0 ? 0 : 1); quit(); });
        });
    }

    void shutdown() override
    {
        if (thread != nullptr && thread->joinable()) thread->join();
    }

private:
    std::unique_ptr<std::thread> thread;
};

START_JUCE_APPLICATION (TestApp)
JUCE_MAIN_FUNCTION_DEFINITION
