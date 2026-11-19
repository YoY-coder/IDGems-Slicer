#include "RandomView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

namespace
{
    void setupLabel (juce::Label& l, const juce::String& t, juce::Component& parent)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (IDGemsLookAndFeel::monoFont (12.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        parent.addAndMakeVisible (l);
    }
}

//==============================================================================
void RandomView::LockButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    auto box = juce::Rectangle<float> (s * 0.7f, s * 0.5f).withCentre ({ r.getCentreX(), r.getCentreY() + s * 0.18f });
    const bool locked = getToggleState();
    const auto c = locked ? Theme::orange : (over ? Theme::text : Theme::textDim);

    juce::Path shackle;
    const float sw = box.getWidth() * 0.62f;
    const float sx = box.getCentreX() - sw * 0.5f;
    shackle.startNewSubPath (sx, box.getY());
    shackle.lineTo (sx, box.getY() - s * 0.18f);
    shackle.quadraticTo (box.getCentreX(), box.getY() - s * 0.45f, sx + sw, box.getY() - s * 0.18f);
    shackle.lineTo (sx + sw, locked ? box.getY() : box.getY() - s * 0.1f);
    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (1.6f));
    if (locked) g.fillRect (box);
    else        g.drawRect (box, 1.4f);
}

//==============================================================================
// Liste des parametres d'une section : cadenas + plage min/max
//==============================================================================
class RandomView::ParamList final : public juce::Component
{
public:
    explicit ParamList (idg::RandomManager& m) : manager (m) {}

    void setSection (int s)
    {
        rows.clear();
        for (auto& e : manager.getEntries())
        {
            if (e.section != s) continue;
            auto row = std::make_unique<Row>();
            row->id = e.id;
            row->name.setText (e.name.toUpperCase(), juce::dontSendNotification);
            row->name.setFont (IDGemsLookAndFeel::monoFont (11.5f));
            row->name.setColour (juce::Label::textColourId, Theme::text);
            row->lock.setTooltip ("Cadenas : ce parametre n'est jamais modifie par le random");
            row->range.setSliderStyle (juce::Slider::TwoValueHorizontal);
            row->range.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row->range.setRange (0.0, 1.0, 0.0);
            row->range.setColour (juce::Slider::trackColourId, e.groove ? Theme::blue : Theme::orange);
            row->range.setTooltip (e.isLane ? "Les lanes sont re-tirees pas par pas (dosage = part des pas modifies)"
                                            : "Plage du random pour ce parametre (min / max)");
            row->range.setEnabled (! e.isLane);
            addAndMakeVisible (row->lock);
            addAndMakeVisible (row->name);
            addAndMakeVisible (row->range);

            auto* r = row.get();
            r->lock.onClick = [this, r] { push (*r); };
            r->range.onValueChange = [this, r] { push (*r); };
            rows.push_back (std::move (row));
        }
        refresh();
        setSize (getWidth(), juce::jmax (1, (int) rows.size() * rowHeight));
        resized();
    }

    void refresh()
    {
        for (auto& r : rows)
        {
            const auto st = manager.getSettings (r->id);
            r->lock.setToggleState (st.locked, juce::dontSendNotification);
            r->range.setMinAndMaxValues (st.min, st.max, juce::dontSendNotification);
            r->name.setAlpha (st.locked ? 0.45f : 1.0f);
        }
    }

    void resized() override
    {
        int y = 0;
        for (auto& r : rows)
        {
            auto line = juce::Rectangle<int> (0, y, getWidth(), rowHeight).reduced (2, 2);
            r->lock.setBounds (line.removeFromLeft (20));
            line.removeFromLeft (4);
            r->name.setBounds (line.removeFromLeft (line.getWidth() / 2 + 10));
            r->range.setBounds (line);
            y += rowHeight;
        }
    }

    static constexpr int rowHeight = 24;

private:
    struct Row
    {
        juce::String id;
        LockButton lock;
        juce::Label name;
        juce::Slider range;
    };

    void push (Row& r)
    {
        manager.setSettings (r.id, { r.lock.getToggleState(), (float) r.range.getMinValue(), (float) r.range.getMaxValue() });
        r.name.setAlpha (r.lock.getToggleState() ? 0.45f : 1.0f);
    }

    idg::RandomManager& manager;
    std::vector<std::unique_ptr<Row>> rows;
};

//==============================================================================
// Morph pad XY entre 4 snapshots
//==============================================================================
class RandomView::MorphPad final : public juce::Component
{
public:
    explicit MorphPad (IDGemsSlicerProcessor& p) : processor (p) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (r);
        g.setColour (Theme::panelBorder);
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4.0f), r.getY(), r.getBottom());
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4.0f), r.getX(), r.getRight());
        }
        g.drawRect (r, 1.0f);

        auto& rm = processor.getRandomManager();
        const char* names[] { "A", "B", "C", "D" };
        const juce::Point<float> corners[] { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() };
        for (int i = 0; i < 4; ++i)
        {
            auto box = juce::Rectangle<float> (22.0f, 22.0f).withCentre (corners[i])
                           .translated (i % 2 == 0 ? 12.0f : -12.0f, i < 2 ? 12.0f : -12.0f);
            const bool has = rm.hasSnapshot (i);
            g.setColour (has ? Theme::sampleColour (i) : Theme::panelBorder);
            g.fillRect (box);
            g.setColour (has ? juce::Colours::black : Theme::textDim);
            g.setFont (IDGemsLookAndFeel::monoFont (13.0f).boldened());
            g.drawText (names[i], box, juce::Justification::centred);
        }

        auto& apvts = processor.getAPVTS();
        const bool on = apvts.getRawParameterValue ("morphOn")->load() > 0.5f;
        const float x = apvts.getRawParameterValue ("morphX")->load() * 0.01f;
        const float y = apvts.getRawParameterValue ("morphY")->load() * 0.01f;
        const juce::Point<float> c { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
        g.setColour (Theme::blue.withAlpha (on ? 0.5f : 0.2f));
        g.fillEllipse (c.x - 14.0f, c.y - 14.0f, 28.0f, 28.0f);
        g.setColour (on ? Theme::orange : Theme::textDim);
        g.drawEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f, 2.0f);
        g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto id : { "morphX", "morphY" }) processor.getAPVTS().getParameter (id)->beginChangeGesture();
        move (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { move (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        for (auto id : { "morphX", "morphY" }) processor.getAPVTS().getParameter (id)->endChangeGesture();
    }

private:
    void move (const juce::MouseEvent& e)
    {
        auto r = getLocalBounds().toFloat();
        const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        auto set = [this] (const char* id, float v)
        {
            auto* p = processor.getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v * 100.0f));
        };
        set ("morphX", x);
        set ("morphY", y);
        repaint();
    }

    IDGemsSlicerProcessor& processor;
};

//==============================================================================
RandomView::RandomView (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();
    auto& rm = processor.getRandomManager();

    randomAllButton.setTooltip ("Randomise toutes les sections non verrouillees (selon le dosage et le mode)");
    randomAllButton.onClick = [this] { processor.getRandomManager().randomiseAll(); };
    randomAllButton.setColour (juce::TextButton::buttonColourId, Theme::orange.withAlpha (0.25f));
    addAndMakeVisible (randomAllButton);

    setupLabel (amountLabel, "DOSAGE", *this);
    amountSlider.setTooltip ("Force du random : leger (les reglages bougent un peu) a total (tout est re-tire)");
    amountSlider.setNumDecimalPlacesToDisplay (0);
    addAndMakeVisible (amountSlider);
    amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "randomAmount", amountSlider);

    setupLabel (modeLabel, "MODE", *this);
    modeBox.addItemList ({ "Tout", "Garder le groove", "Garder les sons" }, 1);
    modeBox.setTooltip ("Garder le groove : fige le rythme (pas, swing, groove, lanes Gate / Ratchet / Proba / Velocite) "
                        "et ne randomise que les sons, slices, samples et FX.\nGarder les sons : l'inverse.");
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "randomMode", modeBox);

    setupLabel (seedLabel, "SEED", *this);
    seedEditor.setInputRestrictions (7, "0123456789");
    seedEditor.setJustification (juce::Justification::centred);
    seedEditor.setFont (IDGemsLookAndFeel::monoFont (13.0f));
    seedEditor.setTooltip ("Numero du dernier tirage. Cochez FIXE et entrez un numero pour retrouver un resultat.");
    seedEditor.onReturnKey = seedEditor.onFocusLost = [this]
    {
        processor.getRandomManager().setSeed (juce::jmax (1, seedEditor.getText().getIntValue()));
    };
    addAndMakeVisible (seedEditor);
    fixedSeedToggle.setTooltip ("Seed fixe : le prochain random utilise ce numero (sinon un nouveau numero a chaque fois)");
    fixedSeedToggle.onClick = [this] { processor.getRandomManager().fixedSeed = fixedSeedToggle.getToggleState(); };
    addAndMakeVisible (fixedSeedToggle);

    backButton.setTooltip ("Revient a l'etat precedent de l'historique des random");
    forwardButton.setTooltip ("Reapplique l'etat suivant de l'historique");
    backButton.onClick = [this] { processor.getRandomManager().goBack(); };
    forwardButton.onClick = [this] { processor.getRandomManager().goForward(); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (forwardButton);
    historyLabel.setJustificationType (juce::Justification::centred);
    setupLabel (historyLabel, "", *this);

    for (int i = 0; i < 6; ++i)
    {
        auto row = std::make_unique<SectionRow>();
        row->lock.setTooltip ("Verrouille toute la section " + idg::RandomManager::sectionNames()[i]);
        row->lock.onClick = [this, i, r = row.get()] { processor.getRandomManager().setSectionLocked (i, r->lock.getToggleState()); };
        row->name.setButtonText (idg::RandomManager::sectionNames()[i]);
        row->name.setTooltip ("Afficher les parametres de cette section (cadenas et plages)");
        row->name.setClickingTogglesState (false);
        row->name.onClick = [this, i] { selectSection (i); };
        row->random.setTooltip ("Random de cette section uniquement");
        row->random.onClick = [this, i] { processor.getRandomManager().randomiseSection (i); };
        addAndMakeVisible (row->lock);
        addAndMakeVisible (row->name);
        addAndMakeVisible (row->random);
        sectionRows[(size_t) i] = std::move (row);
    }

    mutateToggle.setTooltip ("Le pattern evolue tout seul : toutes les N mesures, une petite partie des reglages est re-tiree");
    addAndMakeVisible (mutateToggle);
    mutateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "mutateOn", mutateToggle);
    setupLabel (mutateBarsLabel, "TOUTES LES", *this);
    addAndMakeVisible (mutateBars);
    mutateBarsAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mutateBars", mutateBars);
    mutateBars.textFromValueFunction = [] (double v) { return juce::String ((int) v) + ((int) v > 1 ? " mesures" : " mesure"); };
    mutateBars.updateText();
    setupLabel (mutateRateLabel, "TAUX", *this);
    mutateRate.setNumDecimalPlacesToDisplay (0);
    mutateRate.setTooltip ("Part des reglages modifies a chaque mutation (le pattern evolue sans tout casser)");
    addAndMakeVisible (mutateRate);
    mutateRateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mutateRate", mutateRate);

    paramList = std::make_unique<ParamList> (rm);
    paramViewport.setViewedComponent (paramList.get(), false);
    paramViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (paramViewport);

    morphPad = std::make_unique<MorphPad> (processor);
    addAndMakeVisible (*morphPad);
    morphToggle.setTooltip ("Le pad XY melange les 4 snapshots A, B, C, D (capturez-les d'abord). Automatisable dans Ableton.");
    addAndMakeVisible (morphToggle);
    morphAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "morphOn", morphToggle);
    const char* names[] { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        captureButtons[(size_t) i].setButtonText ("CAPTURER " + juce::String (names[i]));
        captureButtons[(size_t) i].setTooltip ("Memorise les reglages actuels dans le coin " + juce::String (names[i]) + " du morph pad");
        captureButtons[(size_t) i].onClick = [this, i] { processor.getRandomManager().captureSnapshot (i); morphPad->repaint(); };
        addAndMakeVisible (captureButtons[(size_t) i]);
    }

    rm.addChangeListener (this);
    selectSection (0);
    refresh();
}

RandomView::~RandomView()
{
    processor.getRandomManager().removeChangeListener (this);
}

void RandomView::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void RandomView::refresh()
{
    auto& rm = processor.getRandomManager();
    if (! seedEditor.hasKeyboardFocus (true))
        seedEditor.setText (juce::String (rm.getSeed()), juce::dontSendNotification);
    fixedSeedToggle.setToggleState (rm.fixedSeed, juce::dontSendNotification);
    historyLabel.setText (rm.historyText(), juce::dontSendNotification);
    backButton.setEnabled (rm.canGoBack());
    forwardButton.setEnabled (rm.canGoForward());
    for (int i = 0; i < 6; ++i)
    {
        sectionRows[(size_t) i]->lock.setToggleState (rm.isSectionLocked (i), juce::dontSendNotification);
        sectionRows[(size_t) i]->name.setToggleState (i == selectedSection, juce::dontSendNotification);
        sectionRows[(size_t) i]->random.setEnabled (! rm.isSectionLocked (i));
    }
    paramList->refresh();
    morphPad->repaint();
}

void RandomView::selectSection (int s)
{
    selectedSection = s;
    paramList->setSize (paramViewport.getWidth() - paramViewport.getScrollBarThickness(), paramList->getHeight());
    paramList->setSection (s);
    refresh();
    repaint (middleArea);
}

void RandomView::tick()
{
    morphPad->repaint();
}

void RandomView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    leftArea = r.removeFromLeft (290);
    r.removeFromLeft (12);
    rightArea = r.removeFromRight (230);
    r.removeFromRight (12);
    middleArea = r;

    // Colonne gauche
    {
        auto a = leftArea;
        randomAllButton.setBounds (a.removeFromTop (34));
        a.removeFromTop (8);
        auto line = [&a] (juce::Component& l, juce::Component& c, int lw)
        {
            auto row = a.removeFromTop (22);
            l.setBounds (row.removeFromLeft (lw));
            c.setBounds (row);
            a.removeFromTop (5);
        };
        line (amountLabel, amountSlider, 64);
        line (modeLabel, modeBox, 64);
        {
            auto row = a.removeFromTop (22);
            seedLabel.setBounds (row.removeFromLeft (64));
            fixedSeedToggle.setBounds (row.removeFromRight (70));
            row.removeFromRight (6);
            seedEditor.setBounds (row);
            a.removeFromTop (5);
        }
        {
            auto row = a.removeFromTop (22);
            backButton.setBounds (row.removeFromLeft (92));
            forwardButton.setBounds (row.removeFromRight (92));
            historyLabel.setBounds (row);
            a.removeFromTop (10);
        }
        sectionsHeader = a.removeFromTop (16);
        a.removeFromTop (4);
        for (auto& sr : sectionRows)
        {
            auto row = a.removeFromTop (22);
            sr->lock.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (4);
            sr->random.setBounds (row.removeFromRight (48));
            row.removeFromRight (4);
            sr->name.setBounds (row);
            a.removeFromTop (3);
        }
        a.removeFromTop (8);
        mutateArea = a;
        mutateToggle.setBounds (a.removeFromTop (22));
        a.removeFromTop (4);
        line (mutateBarsLabel, mutateBars, 84);
        line (mutateRateLabel, mutateRate, 84);
    }

    // Colonne du milieu : parametres de la section
    {
        auto a = middleArea;
        a.removeFromTop (20);
        paramViewport.setBounds (a);
        paramList->setSize (a.getWidth() - paramViewport.getScrollBarThickness(), juce::jmax (paramList->getHeight(), 10));
        paramList->resized();
    }

    // Colonne droite : morph
    {
        auto a = rightArea;
        a.removeFromTop (20);
        morphToggle.setBounds (a.removeFromTop (22));
        a.removeFromTop (6);
        auto pad = a.removeFromTop (juce::jmin (a.getWidth(), a.getHeight() - 70));
        morphPad->setBounds (pad);
        a.removeFromTop (8);
        auto r1 = a.removeFromTop (24);
        captureButtons[0].setBounds (r1.removeFromLeft (r1.getWidth() / 2 - 3));
        captureButtons[1].setBounds (r1.removeFromRight (r1.getWidth() - 6));
        a.removeFromTop (6);
        auto r2 = a.removeFromTop (24);
        captureButtons[2].setBounds (r2.removeFromLeft (r2.getWidth() / 2 - 3));
        captureButtons[3].setBounds (r2.removeFromRight (r2.getWidth() - 6));
    }
}

void RandomView::paint (juce::Graphics& g)
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
    g.drawText ("RANDOM", r.removeFromTop (20), juce::Justification::centredLeft);

    g.setFont (IDGemsLookAndFeel::monoFont (12.0f).boldened());
    g.setColour (Theme::blue);
    g.drawText ("SECTIONS", sectionsHeader, juce::Justification::centredLeft);
    g.drawText ("PARAMETRES : " + idg::RandomManager::sectionNames()[selectedSection] + " (cadenas, plage)",
                middleArea.withHeight (16), juce::Justification::centredLeft);
    g.drawText ("MORPH PAD", rightArea.withHeight (16), juce::Justification::centredLeft);
}
