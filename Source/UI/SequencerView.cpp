#include "SequencerView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"
#include <cstring>

namespace
{
    constexpr int headerWidth = 196;

    juce::Colour laneColour (int lane)
    {
        switch (lane)
        {
            case idg::laneSample:      return Theme::text;
            case idg::laneSlice:       return Theme::blue;
            case idg::lanePitch:       return Theme::blue.brighter (0.3f);
            case idg::laneReverse:     return Theme::orange;
            case idg::laneGate:        return Theme::orange;
            case idg::laneStart:       return Theme::orange.brighter (0.3f);
            case idg::laneVelocity:    return Theme::orange;
            case idg::laneRatchet:     return Theme::blue;
            case idg::laneProbability: return Theme::blue.brighter (0.2f);
            default:                   return Theme::textDim;
        }
    }
}

//==============================================================================
// Grille de toutes les lanes (dessin + edition a la souris)
//==============================================================================
class SequencerView::Grid final : public juce::Component
{
public:
    explicit Grid (IDGemsSlicerProcessor& p) : processor (p) {}

    int numColumns() const
    {
        int m = 16;
        for (auto& l : processor.getPatternStore().get().lanes) m = juce::jmax (m, l.length);
        return m;
    }

    float rowHeight() const { return (float) getHeight() / (float) idg::numLanes; }

    juce::Rectangle<float> cellBounds (int lane, int step) const
    {
        const float cw = (float) getWidth() / (float) numColumns();
        return { step * cw, lane * rowHeight(), cw, rowHeight() };
    }

    void tick()
    {
        const auto s = processor.currentSeqStep.load();
        if (s != lastStep) { lastStep = s; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& pat = processor.getPatternStore().get();
        const int cols = numColumns();
        const float rh = rowHeight();

        for (int lane = 0; lane < idg::numLanes; ++lane)
        {
            const auto& l = pat.lanes[(size_t) lane];
            const auto& info = idg::laneInfo (lane);
            const auto col = laneColour (lane);
            const int playStep = lastStep >= 0 ? (int) (lastStep % l.length) : -1;

            for (int s = 0; s < cols; ++s)
            {
                auto c = cellBounds (lane, s).reduced (1.0f, 2.0f);
                const bool inside = s < l.length;
                const float dim = (! l.enabled ? 0.3f : 1.0f) * (inside ? 1.0f : 0.22f);

                // Fond (temps forts marques)
                g.setColour ((s % 4 == 0 ? Theme::panelBorder.brighter (0.15f) : Theme::panelBorder).withMultipliedAlpha (inside ? 0.8f : 0.3f));
                g.fillRect (c);

                if (s == playStep && l.enabled)
                {
                    g.setColour (Theme::text.withAlpha (0.25f));
                    g.fillRect (c);
                }

                const float v = l.values[(size_t) s];
                const float norm = (v - info.minValue) / (info.maxValue - info.minValue);

                switch (lane)
                {
                    case idg::laneSample:
                    {
                        const int sv = (int) v;
                        if (sv == 0)
                        {
                            g.setColour (Theme::textDim.withMultipliedAlpha (dim));
                            g.setFont (IDGemsLookAndFeel::monoFont (juce::jmin (11.0f, c.getHeight() * 0.6f)));
                            g.drawText ("A", c, juce::Justification::centred);
                        }
                        else
                        {
                            const auto sc = Theme::sampleColour (sv - 1);
                            g.setColour (sc.withMultipliedAlpha (0.85f * dim));
                            g.fillRect (c.reduced (1.0f));
                            g.setColour (juce::Colours::black.withMultipliedAlpha (dim));
                            g.setFont (IDGemsLookAndFeel::monoFont (juce::jmin (11.0f, c.getHeight() * 0.6f)).boldened());
                            g.drawText (juce::String (sv), c, juce::Justification::centred);
                        }
                        break;
                    }
                    case idg::laneReverse:
                    case idg::laneFreeze:
                        if (v > 0.5f)
                        {
                            g.setColour (col.withMultipliedAlpha (dim));
                            g.fillRect (c.reduced (c.getWidth() * 0.18f, c.getHeight() * 0.22f));
                        }
                        break;
                    case idg::lanePitch:
                    {
                        const float mid = c.getCentreY();
                        const float h = (v / 24.0f) * c.getHeight() * 0.5f;
                        g.setColour (col.withMultipliedAlpha (dim));
                        if (std::abs (v) < 0.5f)
                            g.fillRect (c.getX() + 1.0f, mid - 0.5f, c.getWidth() - 2.0f, 1.0f);
                        else
                            g.fillRect (juce::Rectangle<float> (c.getX() + 1.0f, juce::jmin (mid, mid - h), c.getWidth() - 2.0f, std::abs (h)));
                        break;
                    }
                    case idg::laneRatchet:
                    {
                        const int n = (int) v;
                        g.setColour (col.withMultipliedAlpha (dim));
                        const float w = (c.getWidth() - 2.0f) / (float) n;
                        for (int k = 0; k < n; ++k)
                            g.fillRect (c.getX() + 1.0f + k * w, c.getY() + c.getHeight() * 0.3f, juce::jmax (1.0f, w - 1.0f), c.getHeight() * 0.4f);
                        break;
                    }
                    default:
                    {
                        auto bar = c.withTop (c.getBottom() - c.getHeight() * norm);
                        g.setColour (col.withMultipliedAlpha (0.7f * dim));
                        g.fillRect (bar);
                        g.setColour (col.withMultipliedAlpha (dim));
                        g.fillRect (bar.withHeight (juce::jmin (2.0f, bar.getHeight())));
                        break;
                    }
                }
            }

            // Fin de lane
            if (l.length < cols)
            {
                const float x = cellBounds (lane, l.length).getX();
                g.setColour (Theme::orange.withAlpha (0.8f));
                g.fillRect (x - 1.0f, lane * rh + 2.0f, 2.0f, rh - 4.0f);
            }
        }

        // Valeur survolee
        if (hoverLane >= 0 && hoverStep >= 0)
        {
            const float v = processor.getPatternStore().get().lanes[(size_t) hoverLane].values[(size_t) hoverStep];
            auto c = cellBounds (hoverLane, hoverStep);
            g.setColour (Theme::text);
            g.drawRect (c, 1.0f);
            const auto txt = idg::formatLaneValue (hoverLane, v);
            g.setFont (IDGemsLookAndFeel::monoFont (11.0f));
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 8.0f;
            auto tip = juce::Rectangle<float> (c.getCentreX() - tw * 0.5f, c.getY() - 16.0f, tw, 15.0f);
            if (tip.getY() < 0) tip.setY (c.getBottom() + 1.0f);
            tip = tip.withX (juce::jlimit (0.0f, (float) getWidth() - tw, tip.getX()));
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillRect (tip);
            g.setColour (Theme::text);
            g.drawText (txt, tip, juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { updateHover (e); }
    void mouseExit (const juce::MouseEvent&) override { hoverLane = hoverStep = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragLane = laneAt (e.position.y);
        if (dragLane < 0) return;
        const int step = stepAt (e.position.x);
        if (step < 0) return;

        auto& store = processor.getPatternStore();
        const auto& info = idg::laneInfo (dragLane);
        const float current = store.get().lanes[(size_t) dragLane].values[(size_t) step];

        if (e.mods.isPopupMenu())
            paintValue = info.defaultValue;                         // clic droit : valeur par defaut
        else if (dragLane == idg::laneSample)
            paintValue = (float) (((int) current + 1) % 5);         // clic : Auto > 1 > 2 > 3 > 4
        else if (dragLane == idg::laneReverse || dragLane == idg::laneFreeze)
            paintValue = current > 0.5f ? 0.0f : 1.0f;              // bascule
        else
            paintValue = valueFromY (dragLane, e.position.y);

        if (! store.get().lanes[(size_t) dragLane].enabled)
            store.setEnabled (dragLane, true);
        store.setValue (dragLane, step, paintValue);
        updateHover (e);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragLane < 0) return;
        const int step = stepAt (e.position.x);
        if (step < 0) return;
        const bool continuous = ! e.mods.isPopupMenu() && dragLane != idg::laneSample
                             && dragLane != idg::laneReverse && dragLane != idg::laneFreeze;
        processor.getPatternStore().setValue (dragLane, step, continuous ? valueFromY (dragLane, e.position.y) : paintValue);
        hoverLane = dragLane;
        hoverStep = step;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragLane = -1; }

    std::function<void()> onChange;

private:
    int laneAt (float y) const
    {
        const int l = (int) (y / rowHeight());
        return juce::isPositiveAndBelow (l, idg::numLanes) ? l : -1;
    }

    int stepAt (float x) const
    {
        const int s = (int) (x / ((float) getWidth() / (float) numColumns()));
        return juce::isPositiveAndBelow (s, idg::maxSteps) ? juce::jmin (s, numColumns() - 1) : -1;
    }

    float valueFromY (int lane, float y) const
    {
        const auto& info = idg::laneInfo (lane);
        auto c = cellBounds (lane, 0).reduced (0.0f, 2.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (c.getBottom() - y) / c.getHeight());
        return info.minValue + norm * (info.maxValue - info.minValue);
    }

    void updateHover (const juce::MouseEvent& e)
    {
        hoverLane = laneAt (e.position.y);
        hoverStep = stepAt (e.position.x);
        repaint();
    }

    IDGemsSlicerProcessor& processor;
    juce::int64 lastStep = -1;
    int dragLane = -1, hoverLane = -1, hoverStep = -1;
    float paintValue = 0.0f;
};

//==============================================================================
SequencerView::SequencerView (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();

    grid = std::make_unique<Grid> (processor);
    addAndMakeVisible (*grid);

    seqOnToggle.setTooltip ("Active le sequenceur : il tourne tant qu'une note MIDI est tenue, cale sur le tempo d'Ableton.");
    addAndMakeVisible (seqOnToggle);
    seqOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "seqOn", seqOnToggle);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (IDGemsLookAndFeel::monoFont (12.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (l);
    };

    setupLabel (rateLabel, "PAS");
    rateBox.addItemList (idg::seqRateNames(), 1);
    rateBox.setTooltip ("Duree d'un pas (T = triolet)");
    addAndMakeVisible (rateBox);
    rateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "seqRate", rateBox);

    setupLabel (swingLabel, "SWING");
    swingSlider.setTooltip ("Retarde un pas sur deux");
    swingSlider.setNumDecimalPlacesToDisplay (0);
    addAndMakeVisible (swingSlider);
    swingAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "swing", swingSlider);

    setupLabel (grooveLabel, "GROOVE");
    grooveBox.addItemList (idg::grooveNames(), 1);
    grooveBox.setTooltip ("Gabarit de groove : micro-decalages et accents sur 16 pas");
    addAndMakeVisible (grooveBox);
    grooveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "groove", grooveBox);

    randomAllButton.setTooltip ("Randomise toutes les lanes actives (sauf Freeze et Envoi FX)");
    randomAllButton.onClick = [this]
    {
        processor.getPatternStore().randomiseAll (random);
        refresh();
    };
    addAndMakeVisible (randomAllButton);

    for (int i = 0; i < idg::numLanes; ++i)
    {
        auto row = std::make_unique<LaneRow>();
        const auto& info = idg::laneInfo (i);

        row->enabled.setButtonText (info.name);
        row->enabled.setTooltip (juce::String (info.tooltip) + "\n(case cochee = lane active ; decochee = valeur par defaut)");
        row->enabled.onClick = [this, i, r = row.get()]
        {
            processor.getPatternStore().setEnabled (i, r->enabled.getToggleState());
            grid->repaint();
        };
        addAndMakeVisible (row->enabled);

        row->length.setRange (1, idg::maxSteps, 1);
        row->length.setTooltip ("Longueur de cette lane, en pas (chaque lane a la sienne : polyrythmie)");
        row->length.textFromValueFunction = [] (double v) { return juce::String ((int) v); };
        row->length.setColour (juce::Slider::trackColourId, Theme::blue);
        row->length.onValueChange = [this, i, r = row.get()]
        {
            processor.getPatternStore().setLength (i, (int) r->length.getValue());
            grid->repaint();
        };
        addAndMakeVisible (row->length);

        row->random.setTooltip ("Randomise cette lane");
        row->random.onClick = [this, i]
        {
            processor.getPatternStore().randomiseLane (i, random);
            refresh();
        };
        addAndMakeVisible (row->random);

        laneRows[(size_t) i] = std::move (row);
    }

    refresh();
}

SequencerView::~SequencerView() = default;

void SequencerView::refresh()
{
    const auto& pat = processor.getPatternStore().get();
    for (int i = 0; i < idg::numLanes; ++i)
    {
        auto& row = *laneRows[(size_t) i];
        row.enabled.setToggleState (pat.lanes[(size_t) i].enabled, juce::dontSendNotification);
        row.length.setValue (pat.lanes[(size_t) i].length, juce::dontSendNotification);
    }
    grid->repaint();
}

void SequencerView::refreshIfPatternChanged()
{
    // La banque peut changer au MIDI ou par undo : on resynchronise les longueurs / on-off
    const int cur = processor.getPatternStore().getCurrentIndex();
    const auto& pat = processor.getPatternStore().get();
    bool differs = cur != shownPattern;
    for (int i = 0; i < idg::numLanes && ! differs; ++i)
        differs = laneRows[(size_t) i]->enabled.getToggleState() != pat.lanes[(size_t) i].enabled
               || (int) laneRows[(size_t) i]->length.getValue() != pat.lanes[(size_t) i].length;
    if (differs)
    {
        shownPattern = cur;
        refresh();
    }
    else if (std::memcmp (&shownCopy, &pat, sizeof (idg::Pattern)) != 0)
    {
        grid->repaint();   // valeurs changees (mutate, morph, undo...)
    }
    shownCopy = pat;
}

void SequencerView::tick()
{
    grid->tick();
    const auto step = processor.currentSeqStep.load();
    if (step != lastHeaderStep)
    {
        lastHeaderStep = step;
        repaint (getLocalBounds().removeFromTop (30));
    }
}

void SequencerView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (22);   // titre

    auto top = r.removeFromTop (24);
    seqOnToggle.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (10);
    rateLabel.setBounds (top.removeFromLeft (30));
    rateBox.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (14);
    swingLabel.setBounds (top.removeFromLeft (50));
    swingSlider.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (14);
    grooveLabel.setBounds (top.removeFromLeft (56));
    grooveBox.setBounds (top.removeFromLeft (110));
    randomAllButton.setBounds (top.removeFromRight (130));

    r.removeFromTop (6);
    r.removeFromBottom (18);   // aide

    namesArea = r.removeFromLeft (headerWidth);
    r.removeFromLeft (6);
    grid->setBounds (r);

    const float rh = (float) namesArea.getHeight() / (float) idg::numLanes;
    for (int i = 0; i < idg::numLanes; ++i)
    {
        auto row = juce::Rectangle<int> (namesArea.getX(), namesArea.getY() + (int) (i * rh),
                                         namesArea.getWidth(), (int) rh).reduced (0, 3);
        auto& lr = *laneRows[(size_t) i];
        lr.random.setBounds (row.removeFromRight (36));
        row.removeFromRight (4);
        lr.length.setBounds (row.removeFromRight (40));
        row.removeFromRight (4);
        lr.enabled.setBounds (row);
    }
}

void SequencerView::paint (juce::Graphics& g)
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
    g.drawText ("SEQUENCEUR  //  LANES", r.removeFromTop (20), juce::Justification::centredLeft);

    const auto step = processor.currentSeqStep.load();
    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
    g.setColour (step >= 0 ? Theme::orange : Theme::textDim);
    g.drawText (step >= 0 ? "EN LECTURE  pas " + juce::String (step + 1) : juce::String ("tenez une note pour lancer"),
                getLocalBounds().reduced (12, 8).removeFromTop (20), juce::Justification::centredRight);

    g.setColour (Theme::textDim);
    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.drawText ("Cliquer / glisser : dessiner | Clic droit : valeur par defaut | SAMPLE : clic = Auto > 1 > 2 > 3 > 4 | "
                "Chiffre = longueur de la lane",
                getLocalBounds().reduced (12, 8).removeFromBottom (16), juce::Justification::centredLeft);
}
