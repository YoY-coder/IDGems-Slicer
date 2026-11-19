#include "MapView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../Engine/SliceAnalysis.h"

//==============================================================================
class MapView::Plot final : public juce::Component,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit Plot (IDGemsSlicerProcessor& p) : processor (p)
    {
        for (auto id : { "mapAxisX", "mapAxisY", "mapRadius", "selectMode", "mapSpread" })
            processor.getAPVTS().addParameterListener (id, this);
    }

    ~Plot() override
    {
        for (auto id : { "mapAxisX", "mapAxisY", "mapRadius", "selectMode", "mapSpread" })
            processor.getAPVTS().removeParameterListener (id, this);
    }

    bool drawMode = false;

    void refresh()
    {
        for (int i = 0; i < idg::numSlots; ++i)
            data[(size_t) i] = processor.getSampleManager().getSlot (i).data;
        repaint();
    }

    void tick()
    {
        const juce::Point<float> c { processor.cursorX.load(), processor.cursorY.load() };
        const int tc = processor.triggerCounter.load();
        if (tc != lastTrigger)
        {
            lastTrigger = tc;
            flashSlot = processor.lastTriggerSlot.load();
            flashSlice = processor.lastTriggerSlice.load();
            flash = 1.0f;
        }
        const bool changed = c != cursor || flash > 0.0f;
        cursor = c;
        flash = juce::jmax (0.0f, flash - 0.07f);
        if (changed) repaint();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto area = plotArea();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (getLocalBounds());

        // Grille
        g.setColour (Theme::panelBorder);
        for (int i = 0; i <= 4; ++i)
        {
            const float x = area.getX() + area.getWidth() * i / 4.0f;
            const float y = area.getY() + area.getHeight() * i / 4.0f;
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        }

        // Noms des axes
        const int ax = axisX(), ay = axisY();
        g.setFont (IDGemsLookAndFeel::monoFont (11.0f));
        g.setColour (Theme::textDim);
        static const char* lo[] { "sombre", "court", "faible", "bruit", "grave" };
        static const char* hi[] { "brillant", "long", "fort", "tonal", "aigu" };
        g.drawText (juce::String (lo[ax]) + "  <  " + idg::featureNames()[ax].toUpperCase() + "  >  " + hi[ax],
                    area.withTop (area.getBottom() + 2).withHeight (14), juce::Justification::centred);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, area.getX() - 8.0f, area.getCentreY()));
            g.drawText (juce::String (lo[ay]) + "  <  " + idg::featureNames()[ay].toUpperCase() + "  >  " + hi[ay],
                        juce::Rectangle<float> (area.getX() - 8.0f - area.getHeight() * 0.5f, area.getCentreY() - 12.0f, area.getHeight(), 12.0f),
                        juce::Justification::centred);
        }

        // Points
        bool any = false;
        for (int s = 0; s < idg::numSlots; ++s)
        {
            if (data[(size_t) s] == nullptr) continue;
            any = true;
            const auto col = Theme::sampleColour (s);
            for (int i = 0; i < data[(size_t) s]->numSlices(); ++i)
            {
                const auto& sl = data[(size_t) s]->slices[(size_t) i];
                const auto& f = featuresOf (s, i);
                const auto p = toScreen ({ f[(size_t) ax], f[(size_t) ay] });
                const float r = 2.5f + 3.0f * sl.features[idg::featEnergy];
                g.setColour (col.withAlpha (0.8f));
                g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);

                if (s == flashSlot && i == flashSlice && flash > 0.0f)
                {
                    const float fr = r + 10.0f * flash;
                    g.setColour (col.withAlpha (flash));
                    g.drawEllipse (p.x - fr, p.y - fr, fr * 2.0f, fr * 2.0f, 2.0f);
                }
            }
        }

        if (! any)
        {
            g.setColour (Theme::textDim);
            g.setFont (IDGemsLookAndFeel::monoFont (13.0f));
            g.drawText ("Chargez des samples : chaque slice apparaitra ici comme un point", area, juce::Justification::centred);
        }

        // Trajet
        const auto& path = drawMode ? drawing : processor.getMapStore().get();
        if (path.count >= 2)
        {
            juce::Path p;
            p.startNewSubPath (toScreen (path.points[0]));
            for (int i = 1; i < path.count; ++i) p.lineTo (toScreen (path.points[(size_t) i]));
            const bool on = processor.getAPVTS().getRawParameterValue ("pathOn")->load() > 0.5f;
            g.setColour (Theme::blue.withAlpha (on ? 0.9f : 0.35f));
            juce::Path dashed;
            const float dashes[] { 6.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
            g.fillPath (dashed);
            const auto end = toScreen (path.points[(size_t) path.count - 1]);
            g.fillEllipse (end.x - 3.0f, end.y - 3.0f, 6.0f, 6.0f);
        }

        // Curseur Slice Select + rayon
        const bool mapMode = processor.getAPVTS().getRawParameterValue ("selectMode")->load() > 0.5f;
        const auto c = toScreen (cursor);
        const float radius = processor.getAPVTS().getRawParameterValue ("mapRadius")->load() * 0.01f * area.getWidth();
        if (radius > 1.0f)
        {
            g.setColour (Theme::orange.withAlpha (0.12f));
            g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour (Theme::orange.withAlpha (0.5f));
            g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
        }
        g.setColour (Theme::orange.withAlpha (mapMode ? 1.0f : 0.35f));
        g.drawLine (c.x - 10.0f, c.y, c.x + 10.0f, c.y, 1.5f);
        g.drawLine (c.x, c.y - 10.0f, c.x, c.y + 10.0f, 1.5f);
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);

        if (! mapMode)
        {
            g.setColour (Theme::orange);
            g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
            g.drawText ("Slice Select en mode LINEAIRE : passez la source sur CARTE (panneau JEU, mode Scan / Accord)",
                        area.reduced (6).removeFromTop (16), juce::Justification::centredLeft);
        }

        // Infos de la slice survolee
        if (hoverSlot >= 0 && data[(size_t) hoverSlot] != nullptr && hoverSlice < data[(size_t) hoverSlot]->numSlices())
        {
            const auto& d = *data[(size_t) hoverSlot];
            const auto txt = "S" + juce::String (hoverSlot + 1) + " slice " + juce::String (hoverSlice + 1) + "  |  "
                           + idg::describeSlice (d.slices[(size_t) hoverSlice], d.audio->sampleRate);
            g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 10.0f;
            auto tip = juce::Rectangle<float> (hoverPos.x + 10.0f, hoverPos.y - 22.0f, tw, 16.0f);
            if (tip.getRight() > getWidth()) tip.setX (hoverPos.x - tw - 10.0f);
            if (tip.getY() < 0) tip.setY (hoverPos.y + 10.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillRect (tip);
            g.setColour (Theme::sampleColour (hoverSlot));
            g.drawRect (tip, 1.0f);
            g.setColour (Theme::text);
            g.drawText (txt, tip, juce::Justification::centred);
        }
    }

    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = pointAt (e.position);
        hoverSlot = hit.first;
        hoverSlice = hit.second;
        hoverPos = e.position;
        setMouseCursor (drawMode ? juce::MouseCursor::CrosshairCursor
                                 : hoverSlot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override { hoverSlot = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (drawMode)
        {
            drawing.count = 0;
            addDrawPoint (e.position);
            return;
        }

        const auto hit = pointAt (e.position);
        if (hit.first >= 0 && ! e.mods.isShiftDown())
        {
            processor.requestPreview (hit.first, hit.second);   // clic sur un point = pre-ecoute
            return;
        }

        draggingCursor = true;
        beginGesture();
        moveCursor (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drawMode) { addDrawPoint (e.position); repaint(); return; }
        if (draggingCursor) moveCursor (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drawMode)
        {
            if (drawing.count >= 2) processor.getMapStore().setPath (drawing);
            if (onPathDrawn) onPathDrawn();
            repaint();
            return;
        }
        if (draggingCursor) endGesture();
        draggingCursor = false;
    }

    std::function<void()> onPathDrawn;

private:
    void parameterChanged (const juce::String&, float) override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Plot> (this)] { if (safe) safe->repaint(); });
    }

    // Coordonnees d'une slice (etalees ou brutes, comme le moteur audio)
    const std::array<float, idg::numFeatures>& featuresOf (int s, int i) const
    {
        const auto& t = processor.getSampleManager().getSpreadTable();
        if (processor.getAPVTS().getRawParameterValue ("mapSpread")->load() > 0.5f
            && t.owners[(size_t) s] == data[(size_t) s].get() && t.counts[(size_t) s] == data[(size_t) s]->numSlices())
            return t.values[(size_t) s][(size_t) i];
        return data[(size_t) s]->slices[(size_t) i].features;
    }

    int axisX() const { return juce::jlimit (0, idg::numFeatures - 1, (int) processor.getAPVTS().getRawParameterValue ("mapAxisX")->load()); }
    int axisY() const { return juce::jlimit (0, idg::numFeatures - 1, (int) processor.getAPVTS().getRawParameterValue ("mapAxisY")->load()); }

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (22.0f, 10.0f).withTrimmedBottom (8.0f); }

    juce::Point<float> toScreen (juce::Point<float> n) const
    {
        auto a = plotArea();
        return { a.getX() + n.x * a.getWidth(), a.getBottom() - n.y * a.getHeight() };
    }

    juce::Point<float> fromScreen (juce::Point<float> p) const
    {
        auto a = plotArea();
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
    }

    std::pair<int, int> pointAt (juce::Point<float> pos) const
    {
        std::pair<int, int> best { -1, -1 };
        float bestD = 7.0f;
        const int ax = axisX(), ay = axisY();
        for (int s = 0; s < idg::numSlots; ++s)
        {
            if (data[(size_t) s] == nullptr) continue;
            for (int i = 0; i < data[(size_t) s]->numSlices(); ++i)
            {
                const auto& f = featuresOf (s, i);
                const float d = toScreen ({ f[(size_t) ax], f[(size_t) ay] }).getDistanceFrom (pos);
                if (d < bestD) { bestD = d; best = { s, i }; }
            }
        }
        return best;
    }

    void addDrawPoint (juce::Point<float> pos)
    {
        const auto n = fromScreen (pos);
        if (drawing.count > 0 && drawing.points[(size_t) drawing.count - 1].getDistanceFrom (n) < 0.012f) return;
        if (drawing.count < idg::maxPathPoints) drawing.points[(size_t) drawing.count++] = n;
    }

    void beginGesture()
    {
        for (auto id : { "mapX", "mapY" }) processor.getAPVTS().getParameter (id)->beginChangeGesture();
    }

    void endGesture()
    {
        for (auto id : { "mapX", "mapY" }) processor.getAPVTS().getParameter (id)->endChangeGesture();
    }

    void moveCursor (juce::Point<float> pos)
    {
        const auto n = fromScreen (pos);
        auto set = [this] (const char* id, float v)
        {
            auto* p = processor.getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v * 100.0f));
        };
        set ("mapX", n.x);
        set ("mapY", n.y);
        if (processor.getAPVTS().getRawParameterValue ("pathOn")->load() < 0.5f)
        {
            processor.cursorX.store (n.x);
            processor.cursorY.store (n.y);
        }
        repaint();
    }

    IDGemsSlicerProcessor& processor;
    std::array<idg::SampleData::Ptr, idg::numSlots> data;
    idg::MapPath drawing;
    juce::Point<float> cursor { 0.5f, 0.5f }, hoverPos;
    int hoverSlot = -1, hoverSlice = -1;
    int lastTrigger = 0, flashSlot = -1, flashSlice = -1;
    float flash = 0.0f;
    bool draggingCursor = false;
};

//==============================================================================
MapView::MapView (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();
    plot = std::make_unique<Plot> (processor);
    addAndMakeVisible (*plot);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (IDGemsLookAndFeel::monoFont (12.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (l);
    };

    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    setupLabel (axisXLabel, "AXE X");
    axisXBox.addItemList (idg::featureNames(), 1);
    axisXBox.setTooltip ("Caracteristique placee sur l'axe horizontal");
    addAndMakeVisible (axisXBox);
    axisXAtt = std::make_unique<ComboAtt> (apvts, "mapAxisX", axisXBox);

    setupLabel (axisYLabel, "AXE Y");
    axisYBox.addItemList (idg::featureNames(), 1);
    axisYBox.setTooltip ("Caracteristique placee sur l'axe vertical");
    addAndMakeVisible (axisYBox);
    axisYAtt = std::make_unique<ComboAtt> (apvts, "mapAxisY", axisYBox);

    setupLabel (radiusLabel, "RAYON");
    radiusSlider.setTooltip ("0 = toujours la slice la plus proche du curseur. Plus grand = tirage au hasard parmi les slices du cercle.");
    radiusSlider.setNumDecimalPlacesToDisplay (0);
    addAndMakeVisible (radiusSlider);
    radiusAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "mapRadius", radiusSlider);

    spreadToggle.setTooltip ("Repartit les points sur toute la carte selon leur rang (utile quand les sons se ressemblent). "
                             "Desactive : valeurs mesurees brutes.");
    addAndMakeVisible (spreadToggle);
    spreadAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "mapSpread", spreadToggle);

    pathToggle.setTooltip ("Le curseur parcourt le trajet dessine, en rythme avec Ableton");
    addAndMakeVisible (pathToggle);
    pathAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "pathOn", pathToggle);

    setupLabel (pathLengthLabel, "DUREE");
    pathLengthBox.addItemList (idg::pathLengthNames(), 1);
    pathLengthBox.setTooltip ("Duree d'un parcours complet du trajet");
    addAndMakeVisible (pathLengthBox);
    pathLengthAtt = std::make_unique<ComboAtt> (apvts, "pathLength", pathLengthBox);

    setupLabel (pathModeLabel, "MODE");
    pathModeBox.addItemList (idg::pathModeNames(), 1);
    pathModeBox.setTooltip ("Aller simple : recommence au debut. Ping-pong : aller-retour. Random : point au hasard du trajet a chaque declenchement.");
    addAndMakeVisible (pathModeBox);
    pathModeAtt = std::make_unique<ComboAtt> (apvts, "pathMode", pathModeBox);

    drawButton.setClickingTogglesState (true);
    drawButton.setTooltip ("Active le dessin : tracez un trajet a la souris sur la carte (il remplace le precedent)");
    drawButton.onClick = [this] { plot->drawMode = drawButton.getToggleState(); plot->repaint(); };
    addAndMakeVisible (drawButton);
    plot->onPathDrawn = [this]
    {
        drawButton.setToggleState (false, juce::dontSendNotification);
        plot->drawMode = false;
        if (processor.getMapStore().get().count >= 2)
            if (auto* param = processor.getAPVTS().getParameter ("pathOn"))
                param->setValueNotifyingHost (1.0f);
    };

    clearButton.setTooltip ("Efface le trajet");
    clearButton.onClick = [this]
    {
        processor.getMapStore().clear();
        if (auto* param = processor.getAPVTS().getParameter ("pathOn")) param->setValueNotifyingHost (0.0f);
        plot->repaint();
    };
    addAndMakeVisible (clearButton);

    refresh();
}

MapView::~MapView() = default;

void MapView::refresh() { plot->refresh(); }
void MapView::tick()    { plot->tick(); }

void MapView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (22);

    auto row1 = r.removeFromTop (24);
    axisXLabel.setBounds (row1.removeFromLeft (48));
    axisXBox.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (14);
    axisYLabel.setBounds (row1.removeFromLeft (48));
    axisYBox.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (14);
    radiusLabel.setBounds (row1.removeFromLeft (52));
    radiusSlider.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (14);
    spreadToggle.setBounds (row1.removeFromLeft (100));

    r.removeFromTop (6);
    auto row2 = r.removeFromTop (24);
    pathToggle.setBounds (row2.removeFromLeft (96));
    pathLengthLabel.setBounds (row2.removeFromLeft (48));
    pathLengthBox.setBounds (row2.removeFromLeft (130));
    row2.removeFromLeft (14);
    pathModeLabel.setBounds (row2.removeFromLeft (42));
    pathModeBox.setBounds (row2.removeFromLeft (130));
    clearButton.setBounds (row2.removeFromRight (90));
    row2.removeFromRight (6);
    drawButton.setBounds (row2.removeFromRight (100));

    r.removeFromTop (8);
    r.removeFromBottom (18);
    plot->setBounds (r);
}

void MapView::paint (juce::Graphics& g)
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
    g.setColour (Theme::blue);
    g.strokePath (corner, juce::PathStrokeType (2.0f));
    g.fillRect (a.getX(), a.getBottom() - 2.0f, 60.0f, 2.0f);

    auto r = getLocalBounds().reduced (12, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (Theme::blue);
    g.drawText ("CARTE DES SLICES", r.removeFromTop (20), juce::Justification::centredLeft);

    g.setColour (Theme::textDim);
    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.drawText ("Clic sur un point : ecouter | Clic ailleurs / glisser : deplacer le curseur (Maj : forcer) | "
                "DESSINER puis tracer : trajet",
                getLocalBounds().reduced (12, 8).removeFromBottom (16), juce::Justification::centredLeft);
}
