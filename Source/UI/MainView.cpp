#include "MainView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

MainView::MainView (IDGemsSlicerProcessor& p)
    : processor (p), sliceEditor (p), sequencerView (p), mapView (p), voiceView (p), fxView (p), playPanel (p)
{
    for (int i = 0; i < 4; ++i)
    {
        slotPanels[(size_t) i] = std::make_unique<SlotPanel> (processor, i);
        slotPanels[(size_t) i]->onSelect = [this] (int s) { selectSlot (s); };
        addAndMakeVisible (*slotPanels[(size_t) i]);
    }
    addChildComponent (sliceEditor);
    addChildComponent (sequencerView);
    addChildComponent (mapView);
    addChildComponent (voiceView);
    addChildComponent (fxView);
    addAndMakeVisible (playPanel);

    int tabIndex = 0;
    for (auto* b : { &cutTab, &seqTab, &mapTab, &voiceTab, &fxTab })
    {
        b->setRadioGroupId (1001);
        b->setClickingTogglesState (false);
        b->onClick = [this, i = tabIndex] { selectTab (i); };
        addAndMakeVisible (*b);
        ++tabIndex;
    }
    cutTab.setTooltip ("Decoupe du sample selectionne");
    seqTab.setTooltip ("Sequenceur polyrythmique (une lane par parametre)");
    mapTab.setTooltip ("Carte des slices des 4 samples (Slice Select XY, trajets)");
    voiceTab.setTooltip ("Reglages des voix : enveloppe, pitch clic, filtre, Slice Freeze");
    fxTab.setTooltip ("Chaine FX IDGems et master");
    processor.getSampleManager().addChangeListener (this);

    setSize (IDGemsSlicerProcessor::baseWidth, IDGemsSlicerProcessor::baseHeight);
    setOpaque (true);

    outputGain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    outputGain.setTextValueSuffix (" dB");
    outputGain.setTooltip ("Volume de sortie du plugin (double-clic : 0 dB)");
    outputGain.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    outputGain.setColour (juce::Slider::textBoxTextColourId, Theme::text);
    outputGain.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (outputGain);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), "outputGain", outputGain);

    outputGainLabel.setText ("OUT", juce::dontSendNotification);
    outputGainLabel.setJustificationType (juce::Justification::centred);
    outputGainLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (outputGainLabel);

    buildTexture();
    selectSlot (processor.getSelectedSlot());
    selectTab (processor.getUiProperty ("selectedTab", 0));
    timerCallback();   // premier affichage complet
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    processor.getSampleManager().removeChangeListener (this);
}

void MainView::selectSlot (int s)
{
    processor.setSelectedSlot (s);
    for (int i = 0; i < 4; ++i)
        slotPanels[(size_t) i]->setSelected (i == s);
    sliceEditor.setSlot (s);
}

void MainView::selectTab (int t)
{
    currentTab = juce::jlimit (0, 4, t);
    processor.setUiProperty ("selectedTab", currentTab);
    cutTab.setToggleState (currentTab == 0, juce::dontSendNotification);
    seqTab.setToggleState (currentTab == 1, juce::dontSendNotification);
    mapTab.setToggleState (currentTab == 2, juce::dontSendNotification);
    mapView.setVisible (currentTab == 2);
    voiceTab.setToggleState (currentTab == 3, juce::dontSendNotification);
    voiceView.setVisible (currentTab == 3);
    fxTab.setToggleState (currentTab == 4, juce::dontSendNotification);
    fxView.setVisible (currentTab == 4);
    sliceEditor.setVisible (currentTab == 0);
    sequencerView.setVisible (currentTab == 1);
    if (currentTab == 1) sequencerView.refresh();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& sp : slotPanels) sp->refresh();
    sliceEditor.refresh();
    mapView.refresh();
}

//==============================================================================
void MainView::buildTexture()
{
    // Texture "noir granuleux" generee une seule fois (pas de fichier image)
    texture = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (texture);
    juce::Random r (1234);

    for (int i = 0; i < 9000; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (r.nextFloat() * 0.035f));
        g.fillRect ((float) r.nextInt (getWidth()), (float) r.nextInt (getHeight()), 1.0f, 1.0f);
    }

    // Lignes de balayage tres discretes
    for (int y = 0; y < getHeight(); y += 3)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (0, y, getWidth(), 1);
    }

    // Quelques "fractures" horizontales
    for (int i = 0; i < 14; ++i)
    {
        const int y = r.nextInt (getHeight());
        const int x = r.nextInt (getWidth());
        g.setColour ((r.nextBool() ? Theme::orange : Theme::blue).withAlpha (0.05f + r.nextFloat() * 0.05f));
        g.fillRect (x, y, 40 + r.nextInt (260), 1 + r.nextInt (2));
    }
}

//==============================================================================
void MainView::resized()
{
    auto r = getLocalBounds().reduced (16);

    headerArea = r.removeFromTop (64);
    r.removeFromTop (12);

    slotsArea = r.removeFromTop (150);
    r.removeFromTop (12);

    fxArea = r.removeFromRight (360);
    r.removeFromRight (12);
    mapArea = r;

    {
        auto slots = slotsArea;
        const int w = (slots.getWidth() - 3 * 12) / 4;
        for (int i = 0; i < 4; ++i)
        {
            slotPanels[(size_t) i]->setBounds (slots.removeFromLeft (w));
            slots.removeFromLeft (12);
        }
    }
    {
        auto m = mapArea;
        tabArea = m.removeFromTop (26);
        auto t = tabArea;
        cutTab.setBounds (t.removeFromLeft (130));
        t.removeFromLeft (4);
        seqTab.setBounds (t.removeFromLeft (130));
        t.removeFromLeft (4);
        mapTab.setBounds (t.removeFromLeft (130));
        t.removeFromLeft (4);
        voiceTab.setBounds (t.removeFromLeft (130));
        t.removeFromLeft (4);
        fxTab.setBounds (t.removeFromLeft (130));
        m.removeFromTop (4);
        sliceEditor.setBounds (m);
        sequencerView.setBounds (m);
        mapView.setBounds (m);
        voiceView.setBounds (m);
        fxView.setBounds (m);
    }
    playPanel.setBounds (fxArea);

    // En-tete : volume + vu-metre a droite
    auto h = headerArea;
    meterArea = h.removeFromRight (18);
    h.removeFromRight (10);
    auto knob = h.removeFromRight (70);
    outputGainLabel.setBounds (knob.removeFromTop (14));
    outputGain.setBounds (knob);
    h.removeFromRight (16);
    midiLedArea = h.removeFromRight (70);
}

//==============================================================================
void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.drawImageAt (texture, 0, 0);

    // Vignettage
    {
        juce::ColourGradient vg (juce::Colours::transparentBlack, getLocalBounds().getCentre().toFloat(),
                                 juce::Colours::black.withAlpha (0.55f), { 0.0f, 0.0f }, true);
        g.setGradientFill (vg);
        g.fillAll();
    }

    drawGlitchTitle (g, headerArea.toFloat().withWidth (520.0f));

    // LED MIDI
    {
        auto led = midiLedArea.toFloat();
        auto dot = led.removeFromLeft (16).withSizeKeepingCentre (12, 12);
        g.setColour (Theme::panelBorder);
        g.fillEllipse (dot);
        g.setColour (Theme::orange.withAlpha (midiFlash));
        g.fillEllipse (dot.reduced (1.0f));
        g.setColour (Theme::textDim);
        g.setFont (IDGemsLookAndFeel::monoFont (13.0f));
        g.drawText ("MIDI", led.withTrimmedLeft (6), juce::Justification::centredLeft);
    }

    drawMeter (g, meterArea.toFloat());

    // Ligne sous les onglets
    g.setColour (Theme::orange.withAlpha (0.6f));
    g.fillRect (tabArea.getX(), tabArea.getBottom() + 1, tabArea.getWidth(), 1);
}

void MainView::drawPlaceholder (juce::Graphics& g, juce::Rectangle<int> area,
                                const juce::String& title, const juce::String& note,
                                juce::Colour accent)
{
    auto a = area.toFloat();
    g.setColour (Theme::panel.withAlpha (0.85f));
    g.fillRect (a);
    g.setColour (Theme::panelBorder);
    g.drawRect (a, 1.0f);

    // Coin "decoupe" accentue
    juce::Path corner;
    corner.startNewSubPath (a.getX(), a.getY() + 18.0f);
    corner.lineTo (a.getX(), a.getY());
    corner.lineTo (a.getX() + 18.0f, a.getY());
    g.setColour (accent);
    g.strokePath (corner, juce::PathStrokeType (2.0f));
    g.fillRect (a.getX(), a.getBottom() - 2.0f, juce::jmin (60.0f, a.getWidth()), 2.0f);

    auto t = area.reduced (12, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (accent);
    g.drawText (title, t.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
    g.setColour (Theme::textDim);
    g.drawText (note, t.removeFromTop (18), juce::Justification::centredLeft);
}

void MainView::drawGlitchTitle (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto font = IDGemsLookAndFeel::titleFont (40.0f);
    g.setFont (font);

    auto textArea = area.withTrimmedLeft (4.0f).withTrimmedBottom (10.0f);
    const float baseline = textArea.getCentreY() + font.getAscent() * 0.5f - font.getDescent() * 0.3f;

    // Aberration chromatique : copies orange / bleue decalees.
    // Le decalage augmente avec le niveau de sortie (reagit au son).
    const float off = 1.5f + glitchAmount * 5.0f;

    auto drawWithAberration = [&] (const juce::String& txt, float x, float y)
    {
        g.setColour (Theme::blue.withAlpha (0.7f));
        g.drawSingleLineText (txt, juce::roundToInt (x - off), juce::roundToInt (y));
        g.setColour (Theme::orange.withAlpha (0.7f));
        g.drawSingleLineText (txt, juce::roundToInt (x + off), juce::roundToInt (y));
        g.setColour (Theme::text);
        g.drawSingleLineText (txt, juce::roundToInt (x), juce::roundToInt (y));
    };

    // "IDGEMS " : intact
    const juce::String first ("IDGEMS ");
    float x = textArea.getX();
    drawWithAberration (first, x, baseline);
    x += juce::GlyphArrangement::getStringWidth (font, first);

    // "SLICER" : chaque lettre est tranchee. La moitie haute glisse
    // horizontalement, un fil orange marque la coupe. L'ecart grandit avec le son.
    const juce::String second ("SLICER");
    const float top    = baseline - font.getAscent();
    const float bottom = baseline + font.getDescent();
    juce::Random cutRng (77);   // positions de coupe fixes (seed) pour rester lisible

    for (int i = 0; i < second.length(); ++i)
    {
        const juce::String letter = second.substring (i, i + 1);
        const float w = juce::GlyphArrangement::getStringWidth (font, letter);
        const float capTop = baseline - font.getAscent() * 0.82f;
        const float cutY = capTop + (baseline - capTop) * (0.38f + cutRng.nextFloat() * 0.3f);
        const float dir = (i % 2 == 0) ? 1.0f : -1.0f;
        const float shift = dir * (4.0f + glitchAmount * 8.0f);
        const float lift  = -(3.0f + glitchAmount * 3.0f);   // vide entre les deux moities

        // Moitie basse (immobile)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (juce::Rectangle<float> (x - 12.0f, cutY, w + 24.0f, bottom - cutY + 4.0f).toNearestInt());
            drawWithAberration (letter, x, baseline);
        }

        // Moitie haute (decalee)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (juce::Rectangle<float> (x - 12.0f, top - 4.0f, w + 24.0f, cutY - top + 4.0f).toNearestInt());
            drawWithAberration (letter, x + shift, baseline + lift);
        }

        // Fil de coupe
        g.setColour (Theme::orange.withAlpha (0.9f));
        g.fillRect (x + w * 0.1f, cutY + lift * 0.5f - 0.5f, w * 0.8f, 1.0f);

        x += w;
    }

    // Fracture : quelques bandes horizontales decalees (quand ca joue fort)
    if (glitchAmount > 0.02f)
    {
        const auto titleArea = textArea.withRight (x + 10.0f);
        juce::Random r (glitchSeed);
        const int bands = 1 + (int) (glitchAmount * 4.0f);
        for (int i = 0; i < bands; ++i)
        {
            const float bandH = 2.0f + r.nextFloat() * 4.0f;
            const float y = top + r.nextFloat() * (bottom - top);
            const float dx = (r.nextFloat() - 0.5f) * 30.0f * glitchAmount;
            const auto band = titleArea.withY (y).withHeight (bandH);

            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (band.toNearestInt());
            g.setColour (Theme::background);
            g.fillRect (band);
            g.setColour (r.nextBool() ? Theme::orange : Theme::blue);
            g.drawSingleLineText (first + second, juce::roundToInt (titleArea.getX() + dx), juce::roundToInt (baseline));
        }
    }

    // Sous-titre
    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
    g.setColour (Theme::textDim);
    g.drawText ("IDGEMS // v" + juce::String (JucePlugin_VersionString),
                area.withTrimmedLeft (6.0f).removeFromBottom (12.0f).translated (0.0f, 2.0f),
                juce::Justification::centredLeft);
}

void MainView::drawMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (Theme::panel);
    g.fillRect (area);
    g.setColour (Theme::panelBorder);
    g.drawRect (area, 1.0f);

    auto inner = area.reduced (2.0f);
    const float half = inner.getWidth() * 0.5f;
    const float levels[2] = { meterL, meterR };

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = inner.withWidth (half - 1.0f).translated ((float) ch * half, 0.0f);
        const float db = juce::Decibels::gainToDecibels (levels[ch], -60.0f);
        const float norm = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
        auto fill = bar.removeFromBottom (bar.getHeight() * juce::jlimit (0.0f, 1.0f, norm));
        g.setColour (levels[ch] >= 1.0f ? juce::Colours::red : (norm > 0.85f ? Theme::orange : Theme::blue));
        g.fillRect (fill);
    }
}

//==============================================================================
void MainView::timerCallback()
{
    const float l = processor.getOutputLevel (0);
    const float r = processor.getOutputLevel (1);
    meterL = juce::jmax (l, meterL * 0.85f);
    meterR = juce::jmax (r, meterR * 0.85f);

    const int count = processor.midiActivityCounter.load();
    if (count != lastMidiCount)
    {
        lastMidiCount = count;
        midiFlash = 1.0f;
    }
    else
    {
        midiFlash *= 0.85f;
    }

    // Glitch du titre : pilote par le son + rares sursauts aleatoires
    const float target = juce::jlimit (0.0f, 0.5f, juce::jmax (l, r) * 0.6f);   // reste lisible
    glitchAmount = juce::jmax (target, glitchAmount * 0.8f);
    if (rng.nextFloat() < 0.01f)
        glitchAmount = juce::jmax (glitchAmount, 0.4f);
    glitchSeed = rng.nextInt();

    repaint (headerArea);

    // Panneaux
    std::array<bool, 4> loaded {};
    for (int i = 0; i < 4; ++i) loaded[(size_t) i] = processor.getSampleManager().isLoaded (i);
    const auto probs = idg::Router::normalised (loaded, processor.getProbabilities());
    for (int i = 0; i < 4; ++i) slotPanels[(size_t) i]->tick (probs[(size_t) i]);
    if (sliceEditor.isVisible()) sliceEditor.tick();
    if (sequencerView.isVisible()) sequencerView.tick();
    if (mapView.isVisible()) mapView.tick();
    if (fxView.isVisible()) fxView.tick();
    playPanel.tick (probs, loaded);
}
