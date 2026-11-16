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
}

PlayPanel::PlayPanel (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();

    setupLabel (sourceLabel, "SOURCE", *this);
    sourceBox.addItemList ({ "Auto (aiguillage)", "Sample 1", "Sample 2", "Sample 3", "Sample 4" }, 1);
    sourceBox.setTooltip ("Auto : chaque note tire au sort son sample selon les probabilites.\n"
                          "Sample N : toutes les notes jouent ce sample (si l'emplacement est vide : Auto).");
    addAndMakeVisible (sourceBox);
    sourceAtt = std::make_unique<ComboAtt> (apvts, "keySource", sourceBox);

    setupLabel (rootLabel, "NOTE RACINE", *this);
    rootSlider.setTooltip ("Touche qui joue la slice 1 (notation Ableton : C1 = note 36). "
                           "Les touches suivantes jouent les slices suivantes, en boucle.");
    addAndMakeVisible (rootSlider);
    rootAtt = std::make_unique<SliderAtt> (apvts, "rootNote", rootSlider);
    rootSlider.textFromValueFunction = [] (double v)   // apres l'attachment (qui l'ecrase)
    {
        return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3) + "  (" + juce::String ((int) v) + ")";
    };
    rootSlider.updateText();

    setupLabel (polyLabel, "POLYPHONIE", *this);
    polySlider.setTooltip ("Nombre de slices pouvant sonner en meme temps (1 = mono)");
    addAndMakeVisible (polySlider);
    polyAtt = std::make_unique<SliderAtt> (apvts, "polyphony", polySlider);
    polySlider.textFromValueFunction = [] (double v)
    {
        return (int) v == 1 ? juce::String ("MONO") : juce::String ((int) v) + " voix";
    };
    polySlider.updateText();

    setupLabel (transposeLabel, "TRANSPO", *this);
    transposeSlider.setTextValueSuffix (" st");
    transposeSlider.setNumDecimalPlacesToDisplay (1);
    transposeSlider.setDoubleClickReturnValue (true, 0.0);
    transposeSlider.setTooltip ("Transposition de toutes les slices, en demi-tons (double-clic : 0)");
    addAndMakeVisible (transposeSlider);
    transposeAtt = std::make_unique<SliderAtt> (apvts, "transpose", transposeSlider);

    oneShotToggle.setTooltip ("Active : la slice joue jusqu'au bout meme si vous relachez la touche.\n"
                              "Desactive : la slice s'arrete au relachement.");
    addAndMakeVisible (oneShotToggle);
    oneShotAtt = std::make_unique<ButtonAtt> (apvts, "oneShot", oneShotToggle);

    noRepeatToggle.setTooltip ("Interdit de piocher deux fois de suite dans le meme sample (mode Auto)");
    addAndMakeVisible (noRepeatToggle);
    noRepeatAtt = std::make_unique<ButtonAtt> (apvts, "noRepeat", noRepeatToggle);
}

PlayPanel::~PlayPanel() = default;

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
    r.removeFromTop (22 + 18 + 6);   // titre + mode

    auto row = [&r] (juce::Label& l, juce::Component& c)
    {
        auto line = r.removeFromTop (24);
        l.setBounds (line.removeFromLeft (100));
        c.setBounds (line);
        r.removeFromTop (8);
    };
    row (sourceLabel, sourceBox);
    row (rootLabel, rootSlider);
    row (polyLabel, polySlider);
    row (transposeLabel, transposeSlider);

    auto toggles = r.removeFromTop (24);
    oneShotToggle.setBounds (toggles.removeFromLeft (120));
    noRepeatToggle.setBounds (toggles);
    r.removeFromTop (16);

    distributionArea = r.removeFromTop (54);
    r.removeFromTop (10);
    lastTriggerArea = r.removeFromTop (40);
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
    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
    g.setColour (Theme::textDim);
    g.drawText ("MODE : CLAVIER  (Scan / Accord : etape 4)", r.removeFromTop (20), juce::Justification::centredLeft);

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
            auto box = t.removeFromLeft (24).reduced (4.0f);
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
