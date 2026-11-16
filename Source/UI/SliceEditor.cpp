#include "SliceEditor.h"
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

SliceEditor::SliceEditor (IDGemsSlicerProcessor& p) : processor (p)
{
    addAndMakeVisible (wave);
    wave.onPreview = [this] (int s) { processor.requestPreview (slot, s); };
    wave.onCutsChanged = [this] (std::vector<int> cuts)
    {
        processor.getSampleManager().setManualCuts (slot, std::move (cuts));
    };

    setupLabel (modeLabel, "MODE", *this);
    modeBox.addItemList (idg::sliceModeNames(), 1);
    modeBox.setTooltip ("Methode de decoupe :\n"
                        "- Transitoires : coupe sur les attaques (reglez la sensibilite)\n"
                        "- Grille tempo : coupe en 1/4 a 1/64 au tempo d'Ableton\n"
                        "- Parts egales : N morceaux de meme longueur\n"
                        "- Aleatoire : N coupes au hasard\n"
                        "- Manuel : vos propres marqueurs (s'active des que vous en modifiez un)");
    modeBox.onChange = [this] { settingsChanged(); };
    addAndMakeVisible (modeBox);

    setupLabel (countLabel, "SLICES", *this);
    countSlider.setRange (idg::minSlices, idg::maxSlices, 1);
    countSlider.setTooltip ("Nombre de slices vise (16 a 256). En mode Transitoires : nombre maximum.");
    countSlider.onValueChange = [this] { settingsChanged(); };
    addAndMakeVisible (countSlider);

    setupLabel (sensLabel, "SENSIBILITE", *this);
    sensSlider.setRange (0.0, 100.0, 1.0);
    sensSlider.setTextValueSuffix (" %");
    sensSlider.setTooltip ("Sensibilite de detection des transitoires : plus haut = plus de coupes");
    sensSlider.onValueChange = [this] { settingsChanged(); };
    addAndMakeVisible (sensSlider);

    setupLabel (divLabel, "GRILLE", *this);
    divBox.addItemList (idg::gridDivisionNames(), 1);
    divBox.setTooltip ("Taille de chaque slice en mode Grille tempo (suit le tempo d'Ableton)");
    divBox.onChange = [this] { settingsChanged(); };
    addAndMakeVisible (divBox);

    randomButton.setTooltip ("Nouvelle decoupe aleatoire (nouveau tirage a chaque clic)");
    randomButton.onClick = [this] { processor.getSampleManager().rerollRandom (slot); };
    addAndMakeVisible (randomButton);

    setupLabel (fadeLabel, "ANTI-CLIC", *this);
    fadeSlider.setTooltip ("Micro-fondu au debut et a la fin de chaque slice (tous les samples)");
    fadeSlider.setTextValueSuffix (" ms");
    addAndMakeVisible (fadeSlider);
    fadeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), "fadeMs", fadeSlider);

    normalizeToggle.setTooltip ("Ramene chaque slice au meme niveau crete (tous les samples)");
    addAndMakeVisible (normalizeToggle);
    normalizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), "normalize", normalizeToggle);

    setSlot (processor.getSelectedSlot());
}

SliceEditor::~SliceEditor()
{
    stopTimer();
}

void SliceEditor::setSlot (int s)
{
    if (isTimerRunning()) { stopTimer(); timerCallback(); }   // applique un reglage en attente
    slot = juce::jlimit (0, idg::numSlots - 1, s);
    refresh();
}

void SliceEditor::refresh()
{
    const auto info = processor.getSampleManager().getSlot (slot);
    colour = Theme::sampleColour (slot);
    wave.setData (info.data, colour);

    if (! isTimerRunning())   // ne pas ecraser un reglage en cours de modification
    {
    const juce::ScopedValueSetter<bool> svs (updating, true);
    modeBox.setSelectedItemIndex ((int) info.settings.mode, juce::dontSendNotification);
    countSlider.setValue (info.settings.count, juce::dontSendNotification);
    sensSlider.setValue (info.settings.sensitivity * 100.0, juce::dontSendNotification);
    divBox.setSelectedItemIndex (info.settings.gridDivIndex, juce::dontSendNotification);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &countSlider, &sensSlider })
        c->setColour (juce::Slider::trackColourId, colour);

    header = "DECOUPE  //  SAMPLE " + juce::String (slot + 1);
    if (info.data != nullptr)
        header << "  //  " << info.data->audio->name << "  //  " << info.data->numSlices() << " slices";
    else if (info.loading)
        header << "  //  chargement...";

    updateControlVisibility();
    repaint();
}

void SliceEditor::updateControlVisibility()
{
    const auto mode = (idg::SliceMode) modeBox.getSelectedItemIndex();
    const bool usesCount = mode == idg::SliceMode::transients || mode == idg::SliceMode::equalParts
                        || mode == idg::SliceMode::random;
    countSlider.setEnabled (usesCount);
    countLabel.setEnabled (usesCount);
    sensSlider.setEnabled (mode == idg::SliceMode::transients);
    sensLabel.setEnabled (mode == idg::SliceMode::transients);
    divBox.setEnabled (mode == idg::SliceMode::gridTempo);
    divLabel.setEnabled (mode == idg::SliceMode::gridTempo);
}

void SliceEditor::settingsChanged()
{
    if (updating) return;
    updateControlVisibility();
    startTimer (150);   // on attend que la souris se calme avant de re-decouper
}

void SliceEditor::timerCallback()
{
    stopTimer();
    auto s = processor.getSampleManager().getSlot (slot).settings;
    s.mode = (idg::SliceMode) juce::jmax (0, modeBox.getSelectedItemIndex());
    s.count = (int) countSlider.getValue();
    s.sensitivity = (float) sensSlider.getValue() * 0.01f;
    s.gridDivIndex = juce::jmax (0, divBox.getSelectedItemIndex());
    processor.getSampleManager().setSettings (slot, s);
}

void SliceEditor::tick()
{
    std::vector<float> heads;
    for (auto& d : processor.getVoiceEngine().display)
        if (d.slot.load (std::memory_order_relaxed) == slot)
            heads.push_back (d.position.load (std::memory_order_relaxed));
    wave.setPlayheads (heads);

    const int tc = processor.triggerCounter.load();
    if (tc != lastTriggerCount)
    {
        lastTriggerCount = tc;
        if (processor.lastTriggerSlot.load() == slot)
            wave.flashSlice (processor.lastTriggerSlice.load());
    }
    wave.tick();
}

//==============================================================================
void SliceEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (22);                     // titre
    auto help = r.removeFromBottom (16);
    juce::ignoreUnused (help);
    r.removeFromBottom (6);

    auto row2 = r.removeFromBottom (24);
    r.removeFromBottom (6);
    auto row1 = r.removeFromBottom (24);
    r.removeFromBottom (8);
    wave.setBounds (r);

    auto place = [] (juce::Rectangle<int>& row, juce::Label& l, juce::Component& c, int lw, int cw)
    {
        l.setBounds (row.removeFromLeft (lw));
        c.setBounds (row.removeFromLeft (cw));
        row.removeFromLeft (14);
    };

    place (row1, modeLabel, modeBox, 40, 150);
    place (row1, countLabel, countSlider, 52, 150);
    place (row1, sensLabel, sensSlider, 88, 150);
    place (row1, divLabel, divBox, 52, 80);

    randomButton.setBounds (row2.removeFromLeft (190));
    row2.removeFromLeft (24);
    place (row2, fadeLabel, fadeSlider, 72, 150);
    normalizeToggle.setBounds (row2.removeFromLeft (140));
}

void SliceEditor::paint (juce::Graphics& g)
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
    g.setColour (colour);
    g.strokePath (corner, juce::PathStrokeType (2.0f));
    g.fillRect (a.getX(), a.getBottom() - 2.0f, 60.0f, 2.0f);

    auto r = getLocalBounds().reduced (12, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (colour);
    g.drawText (header, r.removeFromTop (20), juce::Justification::centredLeft);

    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.setColour (Theme::textDim);
    g.drawText ("Clic : ecouter | Double-clic : + marqueur | Glisser : deplacer | "
                "Clic droit : supprimer | Molette : zoom (Maj : defiler)",
                r.removeFromBottom (16), juce::Justification::centredLeft);
}
