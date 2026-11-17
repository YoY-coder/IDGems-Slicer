#include "PlayPanel.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

namespace
{
    void setupLabel (juce::Label& l, const juce::String& text, juce::Component& parent)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (IDGemsLookAndFeel::monoFont (12.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        parent.addAndMakeVisible (l);
    }

    void drawSectionHeader (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (IDGemsLookAndFeel::monoFont (12.0f).boldened());
        g.drawText (text, r, juce::Justification::centredLeft);
        const int tw = juce::GlyphArrangement::getStringWidthInt (IDGemsLookAndFeel::monoFont (12.0f).boldened(), text);
        g.setColour (c.withAlpha (0.35f));
        g.fillRect (r.getX() + tw + 8, r.getCentreY(), r.getWidth() - tw - 8, 1);
    }
}

PlayPanel::PlayPanel (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();

    setupLabel (modeLabel, "MODE", *this);
    modeBox.addItemList ({ "Clavier", "Scan", "Accord" }, 1);
    modeBox.setTooltip ("Clavier : une slice par touche.\n"
                        "Scan : une seule note suffit, le SLICE SELECT choisit la slice.\n"
                        "Accord : une note declenche plusieurs slices decalees dans le temps (meme sample).");
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<ComboAtt> (apvts, "playMode", modeBox);

    setupLabel (sourceLabel, "SOURCE", *this);
    sourceBox.addItemList ({ "Auto (aiguillage)", "Sample 1", "Sample 2", "Sample 3", "Sample 4" }, 1);
    sourceBox.setTooltip ("Jeu direct (sequenceur coupe) :\n"
                          "Auto : chaque note tire au sort son sample selon les probabilites.\n"
                          "Sample N : toutes les notes jouent ce sample (si vide : Auto).\n"
                          "Avec le sequenceur, c'est la lane SAMPLE qui decide.");
    addAndMakeVisible (sourceBox);
    sourceAtt = std::make_unique<ComboAtt> (apvts, "keySource", sourceBox);

    auto& root = addSlider ("rootNote", "NOTE RACINE",
                            "Clavier : touche qui joue la slice 1 (C1 = 36). Scan/Accord : note de reference du pitch.");
    root.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3) + "  (" + juce::String ((int) v) + ")"; };
    root.updateText();

    auto& poly = addSlider ("polyphony", "POLYPHONIE", "Nombre de slices pouvant sonner en meme temps (1 = mono)");
    poly.textFromValueFunction = [] (double v) { return (int) v == 1 ? juce::String ("MONO") : juce::String ((int) v) + " voix"; };
    poly.updateText();

    auto& tr = addSlider ("transpose", "TRANSPO", "Transposition de toutes les slices, en demi-tons (double-clic : 0)");
    tr.setNumDecimalPlacesToDisplay (1);
    tr.setDoubleClickReturnValue (true, 0.0);

    oneShotToggle.setTooltip ("Active : la slice joue jusqu'au bout.\nDesactive : elle s'arrete au relachement (ou a la fin du GATE du sequenceur).");
    noRepeatToggle.setTooltip ("Interdit de piocher deux fois de suite dans le meme sample (aiguillage)");
    pitchFollowToggle.setTooltip ("Scan / Accord : la hauteur suit la note jouee (par rapport a la NOTE RACINE). Sinon : hauteur fixe.");
    for (auto* t : { &oneShotToggle, &noRepeatToggle, &pitchFollowToggle }) addAndMakeVisible (*t);
    oneShotAtt     = std::make_unique<ButtonAtt> (apvts, "oneShot", oneShotToggle);
    noRepeatAtt    = std::make_unique<ButtonAtt> (apvts, "noRepeat", noRepeatToggle);
    pitchFollowAtt = std::make_unique<ButtonAtt> (apvts, "pitchFollow", pitchFollowToggle);

    // Scan
    auto& sel = addSlider ("sliceSelect", "SLICE SELECT",
                           "Position de lecture : 0 % = premiere slice, 100 % = derniere (dans le sample choisi par l'aiguillage). "
                           "Automatisable dans Ableton.");
    sel.setNumDecimalPlacesToDisplay (0);
    addSlider ("scanRandom", "RANDOM", "Ecart aleatoire autour du Slice Select a chaque declenchement").setNumDecimalPlacesToDisplay (0);
    addSlider ("velToScan", "VELOCITE", "La velocite deplace le Slice Select (negatif = vers le debut)").setNumDecimalPlacesToDisplay (0);
    addSlider ("wheelToScan", "MOLETTE", "La molette de modulation deplace le Slice Select").setNumDecimalPlacesToDisplay (0);

    // Accord
    addSlider ("chordCount", "SLICES", "Nombre de slices declenchees par note (Mode Accord)");
    auto& spread = addSlider ("chordSpread", "SPREAD", "Decalage dans le temps entre les slices de l'accord");
    spread.setNumDecimalPlacesToDisplay (0);
    addSlider ("chordStep", "ECART", "Ecart entre les slices de l'accord (1 = slices voisines)");

    for (auto id : { "velToScan", "wheelToScan", "transpose" })
        findRow (id)->slider->setDoubleClickReturnValue (true, 0.0);

    apvts.addParameterListener ("playMode", this);
    updateEnablement();
}

PlayPanel::~PlayPanel()
{
    processor.getAPVTS().removeParameterListener ("playMode", this);
}

juce::Slider& PlayPanel::addSlider (const juce::String& paramId, const juce::String& label, const juce::String& tooltip)
{
    auto row = std::make_unique<Row>();
    setupLabel (row->label, label, *this);
    row->slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
    row->slider->setTooltip (tooltip);
    addAndMakeVisible (*row->slider);
    row->attachment = std::make_unique<SliderAtt> (processor.getAPVTS(), paramId, *row->slider);
    auto& s = *row->slider;
    rows.emplace_back (paramId, std::move (row));
    return s;
}

PlayPanel::Row* PlayPanel::findRow (const juce::String& id)
{
    for (auto& r : rows) if (r.first == id) return r.second.get();
    return nullptr;
}

void PlayPanel::parameterChanged (const juce::String&, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PlayPanel> (this)]
    {
        if (safe != nullptr) safe->updateEnablement();
    });
}

void PlayPanel::updateEnablement()
{
    const int mode = modeBox.getSelectedItemIndex();
    const bool scanLike = mode != 0;
    for (auto id : { "sliceSelect", "scanRandom", "velToScan", "wheelToScan" })
    {
        auto* r = findRow (id);
        r->slider->setEnabled (scanLike);
        r->label.setEnabled (scanLike);
        r->slider->setAlpha (scanLike ? 1.0f : 0.4f);
    }
    for (auto id : { "chordCount", "chordSpread", "chordStep" })
    {
        auto* r = findRow (id);
        r->slider->setEnabled (mode == 2);
        r->slider->setAlpha (mode == 2 ? 1.0f : 0.4f);
    }
    pitchFollowToggle.setEnabled (scanLike);
}

void PlayPanel::tick (const std::array<float, 4>& p, const std::array<bool, 4>& loaded)
{
    bool dirty = p != probs || loaded != slotLoaded;
    probs = p;
    slotLoaded = loaded;

    const int tc = processor.triggerCounter.load();
    if (tc != lastTriggerCount)
    {
        if (lastTriggerCount >= 0) triggerFlash = 1.0f;
        lastTriggerCount = tc;
        dirty = true;
    }
    else if (triggerFlash > 0.0f)
    {
        triggerFlash = juce::jmax (0.0f, triggerFlash - 0.1f);
        dirty = true;
    }

    if (dirty)
    {
        repaint (distributionArea);
        repaint (lastTriggerArea);
    }
}

void PlayPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    constexpr int rowH = 22, gap = 5, labelW = 96;

    auto line = [&] (juce::Component& label, juce::Component& c)
    {
        auto l = r.removeFromTop (rowH);
        label.setBounds (l.removeFromLeft (labelW));
        c.setBounds (l);
        r.removeFromTop (gap);
    };
    auto sliderLine = [&] (const char* id) { auto* row = findRow (id); line (row->label, *row->slider); };
    auto halfLine = [&] (const char* idA, const char* idB)
    {
        auto l = r.removeFromTop (rowH);
        auto left = l.removeFromLeft (l.getWidth() / 2 - 4);
        l.removeFromLeft (8);
        auto* a = findRow (idA);
        auto* b = findRow (idB);
        a->label.setBounds (left.removeFromLeft (64));
        a->slider->setBounds (left);
        b->label.setBounds (l.removeFromLeft (56));
        b->slider->setBounds (l);
        r.removeFromTop (gap);
    };

    line (modeLabel, modeBox);
    line (sourceLabel, sourceBox);
    sliderLine ("rootNote");
    sliderLine ("polyphony");
    sliderLine ("transpose");

    {
        auto l = r.removeFromTop (rowH);
        oneShotToggle.setBounds (l.removeFromLeft (120));
        noRepeatToggle.setBounds (l);
        r.removeFromTop (2);
        pitchFollowToggle.setBounds (r.removeFromTop (rowH));
        r.removeFromTop (gap + 2);
    }

    scanHeader = r.removeFromTop (16);
    r.removeFromTop (4);
    sliderLine ("sliceSelect");
    sliderLine ("scanRandom");
    halfLine ("velToScan", "wheelToScan");

    chordHeader = r.removeFromTop (16);
    r.removeFromTop (4);
    halfLine ("chordCount", "chordStep");
    sliderLine ("chordSpread");

    r.removeFromTop (4);
    distributionArea = r.removeFromTop (42);
    r.removeFromTop (4);
    lastTriggerArea = r.removeFromTop (36);
}

void PlayPanel::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.85f));
    g.fillRect (a);
    g.setColour (Theme::panelBorder);
    g.drawRect (a, 1.0f);

    juce::Path corner;
    corner.startNewSubPath (a.getX(), a.getY() + 18.0f);
    corner.lineTo (a.getX(), a.getY());
    corner.lineTo (a.getX() + 18.0f, a.getY());
    g.setColour (Theme::orange);
    g.strokePath (corner, juce::PathStrokeType (2.0f));
    g.fillRect (a.getX(), a.getBottom() - 2.0f, 60.0f, 2.0f);

    auto r = getLocalBounds().reduced (12, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (Theme::orange);
    g.drawText ("JEU  //  AIGUILLAGE", r.removeFromTop (20), juce::Justification::centredLeft);

    drawSectionHeader (g, scanHeader, "SCAN", Theme::blue);
    drawSectionHeader (g, chordHeader, "ACCORD", Theme::blue);

    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));

    // Repartition reelle des probabilites
    {
        auto d = distributionArea.toFloat();
        g.setColour (Theme::textDim);
        g.drawText ("REPARTITION REELLE", d.removeFromTop (16), juce::Justification::centredLeft);
        auto bar = d.removeFromTop (22);
        g.setColour (Theme::panelBorder);
        g.fillRect (bar);
        float x = bar.getX();
        for (int i = 0; i < 4; ++i)
        {
            const float w = bar.getWidth() * probs[(size_t) i];
            if (w <= 0.0f) continue;
            g.setColour (Theme::sampleColour (i).withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (x, bar.getY(), w, bar.getHeight()));
            if (w > 34.0f)
            {
                g.setColour (juce::Colours::black);
                g.drawText (juce::String (i + 1) + ":" + juce::String (juce::roundToInt (probs[(size_t) i] * 100)) + "%",
                            juce::Rectangle<float> (x, bar.getY(), w, bar.getHeight()), juce::Justification::centred);
            }
            x += w;
        }
        if (! (slotLoaded[0] || slotLoaded[1] || slotLoaded[2] || slotLoaded[3]))
        {
            g.setColour (Theme::textDim);
            g.drawText ("aucun sample charge", bar, juce::Justification::centred);
        }
    }

    // Dernier declenchement
    {
        auto t = lastTriggerArea.toFloat();
        g.setColour (Theme::textDim);
        g.drawText ("DERNIER DECLENCHEMENT", t.removeFromTop (16), juce::Justification::centredLeft);
        const int s = processor.lastTriggerSlot.load();
        const int sl = processor.lastTriggerSlice.load();
        if (s >= 0)
        {
            const auto c = Theme::sampleColour (s);
            auto box = t.removeFromLeft (20).reduced (3.0f);
            g.setColour (c.withAlpha (0.3f + 0.7f * triggerFlash));
            g.fillRect (box);
            g.setColour (c);
            g.setFont (IDGemsLookAndFeel::monoFont (14.0f));
            g.drawText ("  SAMPLE " + juce::String (s + 1) + "  >  SLICE " + juce::String (sl + 1), t,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.drawText ("--", t, juce::Justification::centredLeft);
        }
    }
}
