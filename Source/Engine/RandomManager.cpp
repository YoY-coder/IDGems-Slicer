#include "RandomManager.h"
#include <algorithm>
#include <cstring>

namespace idg
{
namespace IDs
{
    static const juce::Identifier random   { "Random" };
    static const juce::Identifier param    { "Param" };
    static const juce::Identifier id       { "id" };
    static const juce::Identifier locked   { "locked" };
    static const juce::Identifier min      { "min" };
    static const juce::Identifier max      { "max" };
    static const juce::Identifier sections { "sectionLocks" };
    static const juce::Identifier seed     { "seed" };
    static const juce::Identifier morph    { "Morph" };
    static const juce::Identifier corner   { "corner" };
    static const juce::Identifier values   { "values" };
}

const juce::StringArray& RandomManager::sectionNames()
{
    static const juce::StringArray n { "AIGUILLAGE", "DECOUPE", "CARTE", "SEQUENCEUR", "VOIX", "FX" };
    return n;
}

RandomManager::RandomManager (juce::AudioProcessorValueTreeState& a, PatternStore& p, SampleManager& s)
    : apvts (a), patterns (p), samples (s)
{
    buildEntries();
    seed = juce::Random::getSystemRandom().nextInt (999999) + 1;
    startTimerHz (25);
}

RandomManager::~RandomManager() { stopTimer(); }

void RandomManager::buildEntries()
{
    auto add = [this] (const char* id, int section, bool groove = false, float mn = 0.0f, float mx = 1.0f)
    {
        if (auto* p = apvts.getParameter (id))
        {
            entries.push_back ({ id, p->getName (40), section, groove, false, -1 });
            settings[id] = { false, mn, mx };
        }
    };

    for (auto id : { "prob1", "prob2", "prob3", "prob4", "noRepeat" }) add (id, secRouting);
    for (auto id : { "fadeMs", "normalize" }) add (id, secSlicing);
    add ("fadeMs", secSlicing);
    for (auto id : { "mapX", "mapY", "mapRadius", "mapAxisX", "mapAxisY", "pathLength", "pathMode",
                     "sliceSelect", "scanRandom", "velToScan", "wheelToScan" })
        add (id, secMap);
    add ("seqRate", secSequencer, true, 0.15f, 0.65f);   // plutot 1/8 a 1/32
    add ("swing", secSequencer, true, 0.0f, 0.6f);
    add ("groove", secSequencer, true);

    for (int l = 0; l < numLanes; ++l)
    {
        const juce::String id = "lane:" + juce::String (l);
        const bool groove = l == laneGate || l == laneRatchet || l == laneProbability || l == laneVelocity;
        entries.push_back ({ id, juce::String ("Lane ") + laneInfo (l).name, secSequencer, groove, true, l });
        settings[id] = { l == laneFreeze || l == laneFxSend, 0.0f, 1.0f };   // freeze et envoi FX : verrouilles par defaut
    }

    add ("transpose", secVoice, false, 0.25f, 0.75f);
    for (auto id : { "attack" }) add (id, secVoice, false, 0.0f, 0.45f);
    for (auto id : { "decay", "sustain", "release", "clickAmount", "clickTime", "playStart", "playLength",
                     "reverse", "filterType", "cutoff", "resonance", "drive",
                     "freezeOn", "freezePos", "freezeSize", "freezeFade", "chordCount", "chordSpread", "chordStep" })
        add (id, secVoice);
    settings["resonance"].max = 0.6f;
    settings["drive"].max = 0.6f;

    for (auto id : { "fxSend", "crushOn", "crushMix", "crushBits", "crushDown", "fxFilterOn", "fxFilterMix",
                     "fxFilterType", "fxCutoff", "fxReso", "tapeOn", "tapeMix", "tapeTime",
                     "stutterOn", "stutterMix", "stutterDiv", "stutterChance",
                     "grainOn", "grainMix", "grainDensity", "grainSize", "grainSpray", "grainPitch",
                     "ottDepth", "ottTime", "clipOn", "clipDrive" })
        add (id, secFx);
    settings["stutterChance"].max = 0.4f;
    settings["crushBits"].min = 0.15f;
    settings["fxReso"].max = 0.6f;

    // fadeMs ajoute deux fois par erreur : on dedoublonne
    std::vector<ParamEntry> unique;
    for (auto& e : entries)
        if (std::none_of (unique.begin(), unique.end(), [&] (auto& u) { return u.id == e.id; }))
            unique.push_back (e);
    entries = unique;
}

RandomManager::ParamSettings RandomManager::getSettings (const juce::String& id) const
{
    auto it = settings.find (id);
    return it != settings.end() ? it->second : ParamSettings {};
}

void RandomManager::setSettings (const juce::String& id, const ParamSettings& s)
{
    settings[id] = { s.locked, juce::jlimit (0.0f, 1.0f, juce::jmin (s.min, s.max)), juce::jlimit (0.0f, 1.0f, juce::jmax (s.min, s.max)) };
    sendChangeMessage();
}

bool RandomManager::allowed (const ParamEntry& e, int mask) const
{
    if ((mask & (1 << e.section)) == 0) return false;
    if (sectionLocks[(size_t) e.section]) return false;
    if (getSettings (e.id).locked) return false;

    const int mode = (int) apvts.getRawParameterValue ("randomMode")->load();
    if (mode == 1 && e.groove) return false;      // garder le groove : on ne touche pas au rythme
    if (mode == 2 && ! e.groove) return false;    // garder les sons : on ne touche qu'au rythme
    return true;
}

void RandomManager::setParam (juce::RangedAudioParameter* p, float v)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
    p->endChangeGesture();
}

//==============================================================================
void RandomManager::run (int mask, float amount, bool isMutation)
{
    pushHistory();   // etat avant (seulement s'il differe de l'entree courante)

    if (! isMutation && ! fixedSeed)
        seed = juce::Random::getSystemRandom().nextInt (999999) + 1;
    juce::Random r (isMutation ? juce::Random::getSystemRandom().nextInt64() : (juce::int64) seed);

    for (auto& e : entries)
    {
        if (! allowed (e, mask)) continue;
        const auto st = getSettings (e.id);

        if (e.isLane)
        {
            patterns.mutateLane (e.lane, r, amount);
            continue;
        }

        auto* p = apvts.getParameter (e.id);
        if (p == nullptr) continue;
        const float current = p->getValue();
        const float target = st.min + r.nextFloat() * (st.max - st.min);
        const int steps = p->getNumSteps();

        if (steps > 0 && steps < 64)
        {
            // Parametre a crans (choix, on/off) : change ou non, selon le dosage
            if (r.nextFloat() < amount)
                setParam (p, p->convertTo0to1 (p->convertFrom0to1 (target)));
        }
        else
        {
            setParam (p, current + (target - current) * amount);
        }
    }

    // Decoupe : nouveaux reglages pour les samples charges
    if ((mask & (1 << secSlicing)) && ! sectionLocks[secSlicing] && (int) apvts.getRawParameterValue ("randomMode")->load() != 2)
    {
        for (int s = 0; s < numSlots; ++s)
        {
            if (! samples.isLoaded (s) || r.nextFloat() >= amount) continue;
            auto set = samples.getSlot (s).settings;
            static const SliceMode modes[] { SliceMode::transients, SliceMode::equalParts, SliceMode::random, SliceMode::gridTempo };
            set.mode = modes[r.nextInt (4)];
            set.count = 16 + r.nextInt (49);
            set.sensitivity = 0.25f + 0.6f * r.nextFloat();
            set.gridDivIndex = 1 + r.nextInt (3);
            set.seed = r.nextInt (999999) + 1;
            samples.setSettings (s, set);
        }
    }

    // L'etat "apres" est memorise au prochain random (ou au retour en arriere)
    sendChangeMessage();
}

void RandomManager::randomiseAll()
{
    run ((1 << numSections) - 1, apvts.getRawParameterValue ("randomAmount")->load() * 0.01f, false);
}

void RandomManager::randomiseSection (int section)
{
    run (1 << section, apvts.getRawParameterValue ("randomAmount")->load() * 0.01f, false);
}

void RandomManager::mutate (float rate)
{
    // Mutation : toutes les sections non verrouillees, sans la decoupe (trop brutal en lecture)
    run (((1 << numSections) - 1) & ~(1 << secSlicing), rate, true);
}

//==============================================================================
RandomManager::Snapshot RandomManager::capture() const
{
    Snapshot s;
    s.params = apvts.copyState();
    s.pattern = patterns.get();
    for (int i = 0; i < numSlots; ++i) s.slicing[(size_t) i] = samples.getSlot (i).settings;
    return s;
}

void RandomManager::restore (const Snapshot& s)
{
    for (auto& e : entries)
    {
        if (e.isLane) continue;
        if (auto* p = apvts.getParameter (e.id))
        {
            auto child = s.params.getChildWithProperty ("id", e.id);
            if (child.isValid())
                setParam (p, p->convertTo0to1 ((float) (double) child.getProperty ("value")));
        }
    }
    patterns.setPattern (s.pattern);
    for (int i = 0; i < numSlots; ++i)
        if (samples.isLoaded (i) && ! (samples.getSlot (i).settings == s.slicing[(size_t) i])
            && s.slicing[(size_t) i].mode != SliceMode::manual)
            samples.setSettings (i, s.slicing[(size_t) i]);
}

void RandomManager::pushHistory()
{
    auto now = capture();

    // Identique a l'entree courante : rien a memoriser
    if (juce::isPositiveAndBelow (historyIndex, (int) history.size()))
    {
        const auto& cur = history[(size_t) historyIndex];
        if (cur.params.isEquivalentTo (now.params)
            && std::memcmp (&cur.pattern, &now.pattern, sizeof (Pattern)) == 0)
            return;
    }

    // On coupe la "branche avant" si on etait revenu en arriere
    if (historyIndex + 1 < (int) history.size())
        history.erase (history.begin() + historyIndex + 1, history.end());
    history.push_back (std::move (now));
    if (history.size() > 64) history.erase (history.begin());
    historyIndex = (int) history.size() - 1;
}

void RandomManager::goBack()
{
    // L'etat courant (resultat du dernier random ou retouches) est memorise pour "avant"
    pushHistory();
    if (historyIndex <= 0) return;
    --historyIndex;
    restore (history[(size_t) historyIndex]);
    sendChangeMessage();
}

void RandomManager::goForward()
{
    if (! canGoForward()) return;
    ++historyIndex;
    restore (history[(size_t) historyIndex]);
    sendChangeMessage();
}

juce::String RandomManager::historyText() const
{
    if (history.empty()) return "historique vide";
    return juce::String (historyIndex + 1) + " / " + juce::String ((int) history.size());
}

//==============================================================================
void RandomManager::captureSnapshot (int corner)
{
    auto& m = morphSnapshots[(size_t) juce::jlimit (0, 3, corner)];
    m.values.clear();
    for (auto& e : entries)
    {
        auto* p = e.isLane ? nullptr : apvts.getParameter (e.id);
        m.values.push_back (p != nullptr ? p->getValue() : 0.0f);
    }
    m.pattern = patterns.get();
    m.captured = true;
    lastMorphX = lastMorphY = -1.0f;
    sendChangeMessage();
}

void RandomManager::applyMorph (float x, float y)
{
    // Coins : A haut-gauche, B haut-droite, C bas-gauche, D bas-droite (y = 1 en haut)
    std::array<float, 4> w { (1.0f - x) * y, x * y, (1.0f - x) * (1.0f - y), x * (1.0f - y) };
    float total = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (! morphSnapshots[(size_t) i].captured || morphSnapshots[(size_t) i].values.size() != entries.size()) w[(size_t) i] = 0.0f;
        total += w[(size_t) i];
    }
    if (total <= 0.0f) return;
    for (auto& v : w) v /= total;
    const int dominant = (int) std::distance (w.begin(), std::max_element (w.begin(), w.end()));
    Pattern morphed = patterns.get();
    bool laneChanged = false;

    for (size_t k = 0; k < entries.size(); ++k)
    {
        const auto& e = entries[k];
        if (getSettings (e.id).locked || sectionLocks[(size_t) e.section]) continue;

        if (e.isLane)
        {
            // Lanes : valeurs interpolees (ou du coin dominant pour les lanes a crans)
            auto& lane = morphed.lanes[(size_t) e.lane];
            laneChanged = true;
            const auto& info = laneInfo (e.lane);
            for (int s = 0; s < maxSteps; ++s)
            {
                if (info.discrete)
                    lane.values[(size_t) s] = morphSnapshots[(size_t) dominant].pattern.lanes[(size_t) e.lane].values[(size_t) s];
                else
                {
                    float v = 0.0f;
                    for (int c = 0; c < 4; ++c)
                        if (w[(size_t) c] > 0.0f) v += w[(size_t) c] * morphSnapshots[(size_t) c].pattern.lanes[(size_t) e.lane].values[(size_t) s];
                    lane.values[(size_t) s] = v;
                }
            }
            lane.length = morphSnapshots[(size_t) dominant].pattern.lanes[(size_t) e.lane].length;
            lane.enabled = morphSnapshots[(size_t) dominant].pattern.lanes[(size_t) e.lane].enabled;
            continue;
        }

        auto* p = apvts.getParameter (e.id);
        if (p == nullptr) continue;
        float v;
        const int steps = p->getNumSteps();
        if (steps > 0 && steps < 64)
            v = morphSnapshots[(size_t) dominant].values[k];
        else
        {
            v = 0.0f;
            for (int c = 0; c < 4; ++c) v += w[(size_t) c] * (w[(size_t) c] > 0.0f ? morphSnapshots[(size_t) c].values[k] : 0.0f);
        }
        if (std::abs (p->getValue() - v) > 1.0e-4f)
            p->setValueNotifyingHost (v);
    }
    if (laneChanged && std::memcmp (&morphed, &patterns.get(), sizeof (Pattern)) != 0)
        patterns.setPattern (morphed);
}

void RandomManager::timerCallback()
{
    // Mutate demande par le thread audio (toutes les N mesures)
    const int req = mutateRequests.load();
    if (req != lastMutateRequests)
    {
        lastMutateRequests = req;
        mutate (apvts.getRawParameterValue ("mutateRate")->load() * 0.01f);
    }

    // Morph pad
    if (apvts.getRawParameterValue ("morphOn")->load() > 0.5f)
    {
        const float x = apvts.getRawParameterValue ("morphX")->load() * 0.01f;
        const float y = apvts.getRawParameterValue ("morphY")->load() * 0.01f;
        if (std::abs (x - lastMorphX) > 1.0e-4f || std::abs (y - lastMorphY) > 1.0e-4f)
        {
            lastMorphX = x;
            lastMorphY = y;
            applyMorph (x, y);
        }
    }
    else
    {
        lastMorphX = lastMorphY = -1.0f;
    }
}

//==============================================================================
juce::ValueTree RandomManager::toValueTree() const
{
    juce::ValueTree t (IDs::random);
    t.setProperty (IDs::seed, seed, nullptr);
    juce::String locks;
    for (auto l : sectionLocks) locks << (l ? "1" : "0");
    t.setProperty (IDs::sections, locks, nullptr);

    for (auto& [id, st] : settings)
    {
        juce::ValueTree p (IDs::param);
        p.setProperty (IDs::id, id, nullptr);
        p.setProperty (IDs::locked, st.locked, nullptr);
        p.setProperty (IDs::min, st.min, nullptr);
        p.setProperty (IDs::max, st.max, nullptr);
        t.appendChild (p, nullptr);
    }

    for (int c = 0; c < 4; ++c)
    {
        const auto& m = morphSnapshots[(size_t) c];
        if (! m.captured) continue;
        juce::ValueTree mt (IDs::morph);
        mt.setProperty (IDs::corner, c, nullptr);
        juce::StringArray vals;
        for (size_t k = 0; k < entries.size() && k < m.values.size(); ++k)
            if (! entries[k].isLane) vals.add (entries[k].id + "=" + juce::String (m.values[k], 5));
        mt.setProperty (IDs::values, vals.joinIntoString (";"), nullptr);
        PatternStore tmp;
        tmp.setPattern (m.pattern);
        mt.appendChild (tmp.toValueTree(), nullptr);
        t.appendChild (mt, nullptr);
    }
    return t;
}

void RandomManager::fromValueTree (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    seed = (int) t.getProperty (IDs::seed, seed);
    const auto locks = t.getProperty (IDs::sections).toString();
    for (int i = 0; i < numSections && i < locks.length(); ++i) sectionLocks[(size_t) i] = locks[i] == '1';

    for (auto p : t)
        if (p.hasType (IDs::param))
        {
            const auto id = p.getProperty (IDs::id).toString();
            if (settings.count (id))
                settings[id] = { (bool) p.getProperty (IDs::locked), (float) (double) p.getProperty (IDs::min, 0.0),
                                 (float) (double) p.getProperty (IDs::max, 1.0) };
        }

    for (auto& m : morphSnapshots) m.captured = false;
    for (auto mt : t)
        if (mt.hasType (IDs::morph))
        {
            auto& m = morphSnapshots[(size_t) juce::jlimit (0, 3, (int) mt.getProperty (IDs::corner))];
            std::map<juce::String, float> vals;
            for (auto& kv : juce::StringArray::fromTokens (mt.getProperty (IDs::values).toString(), ";", ""))
                vals[kv.upToFirstOccurrenceOf ("=", false, false)] = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            m.values.clear();
            for (auto& e : entries)
            {
                auto* p = e.isLane ? nullptr : apvts.getParameter (e.id);
                m.values.push_back (vals.count (e.id) ? vals[e.id] : (p != nullptr ? p->getValue() : 0.0f));
            }
            PatternStore tmp;
            tmp.fromValueTree (mt.getChildWithName ("Sequencer"));
            m.pattern = tmp.get();
            m.captured = true;
        }
    sendChangeMessage();
}
}
