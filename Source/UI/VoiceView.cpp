#include "VoiceView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

namespace
{
    void drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        auto a = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (a);
        g.setColour (Theme::panelBorder);
        g.drawRect (a, 1.0f);
        g.setColour (c);
        g.fillRect (a.getX(), a.getY(), 14.0f, 2.0f);
        g.setFont (IDGemsLookAndFeel::monoFont (12.0f).boldened());
        g.drawText (title, r.reduced (8, 4).removeFromTop (16), juce::Justification::centredLeft);
    }
}

VoiceView::VoiceView (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();

    addKnob ("attack",  "ATTAQUE",  "Temps de montee du volume");
    addKnob ("decay",   "DECROIS.", "Temps pour descendre au niveau de maintien");
    addKnob ("sustain", "MAINTIEN", "Niveau tant que la note (ou le gate) est tenue");
    addKnob ("release", "RELACHE",  "Temps d'extinction apres le relachement");

    addKnob ("clickAmount", "QUANTITE", "Pitch clic : la lecture demarre plus aigue (en demi-tons) puis revient a la normale. Donne du mordant aux transitoires.");
    addKnob ("clickTime",   "DUREE",    "Pitch clic : temps de retour a la hauteur normale");

    addKnob ("playStart",  "START",    "Point de depart dans chaque slice (s'ajoute a la lane START)");
    addKnob ("playLength", "LONGUEUR", "Portion de la slice jouee");

    addKnob ("cutoff",    "FREQUENCE", "Frequence de coupure du filtre");
    addKnob ("resonance", "RESONANCE", "Resonance du filtre");
    addKnob ("drive",     "DRIVE",     "Saturation avant le filtre");

    addKnob ("freezePos",  "POSITION", "Position du grain fige dans la slice");
    addKnob ("freezeSize", "TAILLE",   "Taille du grain boucle");
    addKnob ("freezeFade", "FONDU",    "Fondu enchaine a chaque boucle du grain (plus = plus doux)");

    reverseToggle.setTooltip ("Lecture a l'envers (inverse aussi la lane REVERSE)");
    addAndMakeVisible (reverseToggle);
    reverseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "reverse", reverseToggle);

    freezeToggle.setTooltip ("Slice Freeze pour toutes les notes. Sinon : activable pas par pas avec la lane FREEZE du sequenceur.");
    addAndMakeVisible (freezeToggle);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "freezeOn", freezeToggle);

    filterBox.addItemList ({ "Off", "Passe-bas", "Passe-haut", "Passe-bande" }, 1);
    filterBox.setTooltip ("Type de filtre (par voix)");
    addAndMakeVisible (filterBox);
    filterAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "filterType", filterBox);

    for (auto id : { "attack", "decay", "sustain", "release" })
        apvts.addParameterListener (id, this);
}

VoiceView::~VoiceView()
{
    for (auto id : { "attack", "decay", "sustain", "release" })
        processor.getAPVTS().removeParameterListener (id, this);
}

void VoiceView::parameterChanged (const juce::String&, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<VoiceView> (this)]
    {
        if (safe != nullptr) safe->repaint (safe->envGraph);
    });
}

VoiceView::Knob& VoiceView::addKnob (const juce::String& id, const juce::String& label, const juce::String& tooltip)
{
    auto k = std::make_unique<Knob>();
    k->label.setText (label, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (IDGemsLookAndFeel::monoFont (11.0f));
    k->label.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (k->label);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    k->slider.setTooltip (tooltip);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getAPVTS(), id, k->slider);
    k->slider.setNumDecimalPlacesToDisplay (id == "resonance" ? 2 : 0);
    if (auto* param = processor.getAPVTS().getParameter (id))
        if (param->getLabel().isNotEmpty())
            k->slider.setTextValueSuffix (" " + param->getLabel());
    auto& ref = *k;
    knobs.emplace_back (id, std::move (k));
    return ref;
}

void VoiceView::placeKnobs (juce::Rectangle<int> area, std::initializer_list<const char*> ids)
{
    const int w = area.getWidth() / (int) ids.size();
    for (auto id : ids)
        for (auto& k : knobs)
            if (k.first == id)
            {
                auto cell = area.removeFromLeft (w).reduced (2, 0);
                k.second->label.setBounds (cell.removeFromTop (14));
                k.second->slider.setBounds (cell);
            }
}

void VoiceView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    const int gap = 10;
    const int rowH = (r.getHeight() - 2 * gap) / 3;

    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (gap);
    auto row2 = r.removeFromTop (rowH);
    r.removeFromTop (gap);
    auto row3 = r;

    envArea = row1.removeFromLeft (row1.getWidth() * 62 / 100);
    row1.removeFromLeft (gap);
    clickArea = row1;

    playArea = row2.removeFromLeft (row2.getWidth() * 38 / 100);
    row2.removeFromLeft (gap);
    filterArea = row2;

    freezeArea = row3;

    // Enveloppe : 4 boutons + graphe
    {
        auto a = envArea.reduced (8, 4);
        a.removeFromTop (18);
        envGraph = a.removeFromRight (a.getWidth() * 30 / 100).reduced (4, 6);
        placeKnobs (a, { "attack", "decay", "sustain", "release" });
    }
    {
        auto a = clickArea.reduced (8, 4);
        a.removeFromTop (18);
        placeKnobs (a, { "clickAmount", "clickTime" });
    }
    {
        auto a = playArea.reduced (8, 4);
        a.removeFromTop (18);
        reverseToggle.setBounds (a.removeFromRight (100).withSizeKeepingCentre (100, 24));
        placeKnobs (a, { "playStart", "playLength" });
    }
    {
        auto a = filterArea.reduced (8, 4);
        auto top = a.removeFromTop (18);
        filterBox.setBounds (top.removeFromRight (140).withHeight (20));
        a.removeFromTop (4);
        placeKnobs (a, { "cutoff", "resonance", "drive" });
    }
    {
        auto a = freezeArea.reduced (8, 4);
        auto top = a.removeFromTop (18);
        freezeToggle.setBounds (top.removeFromRight (150).withHeight (20));
        a.removeFromTop (4);
        a.removeFromRight (a.getWidth() * 25 / 100);
        placeKnobs (a, { "freezePos", "freezeSize", "freezeFade" });
    }
}

void VoiceView::paint (juce::Graphics& g)
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
    g.drawText ("VOIX  //  REGLAGES DES SLICES", r.removeFromTop (20), juce::Justification::centredLeft);

    drawSection (g, envArea, "ENVELOPPE", Theme::orange);
    drawSection (g, clickArea, "PITCH CLIC", Theme::blue);
    drawSection (g, playArea, "LECTURE", Theme::orange);
    drawSection (g, filterArea, "FILTRE + DRIVE", Theme::blue);
    drawSection (g, freezeArea, "SLICE FREEZE", Theme::orange);

    // Graphe ADSR
    {
        auto& apvts = processor.getAPVTS();
        const float at = apvts.getRawParameterValue ("attack")->load();
        const float de = apvts.getRawParameterValue ("decay")->load();
        const float su = apvts.getRawParameterValue ("sustain")->load() * 0.01f;
        const float re = apvts.getRawParameterValue ("release")->load();
        const float hold = juce::jmax (100.0f, (at + de + re) * 0.35f);
        const float total = at + de + hold + re;
        auto gr = envGraph.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (gr);
        auto px = [&] (float t) { return gr.getX() + gr.getWidth() * t / total; };
        juce::Path p;
        p.startNewSubPath (gr.getX(), gr.getBottom());
        p.lineTo (px (at), gr.getY());
        p.lineTo (px (at + de), gr.getBottom() - su * gr.getHeight());
        p.lineTo (px (at + de + hold), gr.getBottom() - su * gr.getHeight());
        p.lineTo (gr.getRight(), gr.getBottom());
        g.setColour (Theme::blue.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (1.5f), juce::AffineTransform::translation (-1.5f, 0.0f));
        g.setColour (Theme::orange);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // Explication du freeze
    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.setColour (Theme::textDim);
    auto fr = freezeArea.reduced (8, 4);
    fr.removeFromTop (22);
    g.drawFittedText ("Fige un grain de la slice et le boucle tant que la note est tenue. "
                      "Pas par pas : lane FREEZE du sequenceur.",
                      fr.removeFromRight (fr.getWidth() * 25 / 100), juce::Justification::centredLeft, 5);
}
