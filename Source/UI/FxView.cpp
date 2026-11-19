#include "FxView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

namespace
{
    std::unique_ptr<FxView::Knob> makeKnob (juce::AudioProcessorValueTreeState& apvts, juce::Component& parent,
                                            const juce::String& id, const juce::String& label, const juce::String& tooltip)
    {
        auto k = std::make_unique<FxView::Knob>();
        k->label.setText (label, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setFont (IDGemsLookAndFeel::monoFont (10.5f));
        k->label.setColour (juce::Label::textColourId, Theme::textDim);
        parent.addAndMakeVisible (k->label);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
        k->slider.setTooltip (tooltip);
        parent.addAndMakeVisible (k->slider);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, k->slider);
        k->slider.setNumDecimalPlacesToDisplay (id == "fxReso" || id == "crushBits" || id == "crushDown" ? 1 : 0);
        if (auto* p = apvts.getParameter (id))
            if (p->getLabel().isNotEmpty()) k->slider.setTextValueSuffix (" " + p->getLabel());
        return k;
    }

    void drawBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c, bool on = true)
    {
        auto a = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (on ? 0.35f : 0.2f));
        g.fillRect (a);
        g.setColour (on ? c.withAlpha (0.7f) : Theme::panelBorder);
        g.drawRect (a, 1.0f);
        g.setColour (c);
        g.fillRect (a.getX(), a.getY(), 14.0f, 2.0f);
        g.setFont (IDGemsLookAndFeel::monoFont (12.0f).boldened());
        g.setColour (on ? c : Theme::textDim);
        g.drawText (title, r.reduced (8, 4).removeFromTop (16), juce::Justification::centredLeft);
    }
}

void FxView::Knob::setBounds (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromTop (13));
    slider.setBounds (r);
}

//==============================================================================
// Carte d'un module (glissable pour changer l'ordre)
//==============================================================================
class FxView::ModuleCard final : public juce::Component,
                                 public juce::DragAndDropTarget
{
public:
    ModuleCard (FxView& o, IDGemsSlicerProcessor& p, int moduleId) : owner (o), processor (p), id (moduleId)
    {
        auto& apvts = processor.getAPVTS();
        static const char* prefix[] { "crush", "fxFilter", "tape", "stutter", "grain" };
        const juce::String pre (prefix[id]);

        onToggle.setButtonText ("ON");
        onToggle.setTooltip ("Active / desactive ce module");
        addAndMakeVisible (onToggle);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pre + "On", onToggle);
        onToggle.onStateChange = [this] { repaint(); };

        knobs.push_back (makeKnob (apvts, *this, pre + "Mix", "DRY/WET", "Melange signal sec / traite de ce module"));

        switch (id)
        {
            case idg::fxBitcrush:
                knobs.push_back (makeKnob (apvts, *this, "crushBits", "BITS", "Resolution (moins = plus sale)"));
                knobs.push_back (makeKnob (apvts, *this, "crushDown", "DOWNSAMPLE", "Reduction de la frequence d'echantillonnage"));
                break;
            case idg::fxFilter:
                combo.addItemList ({ "Passe-bas", "Passe-haut", "Passe-bande" }, 1);
                combo.setTooltip ("Type de filtre");
                addAndMakeVisible (combo);
                comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "fxFilterType", combo);
                knobs.push_back (makeKnob (apvts, *this, "fxCutoff", "FREQUENCE", "Frequence de coupure"));
                knobs.push_back (makeKnob (apvts, *this, "fxReso", "RESONANCE", "Resonance (siffle quand elle est haute)"));
                break;
            case idg::fxTapeStop:
                action.setButtonText ("STOP");
                action.setTooltip ("Ralentit la bande jusqu'a l'arret ; recliquez pour repartir. Automatisable dans Ableton.");
                addAndMakeVisible (action);
                actionAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "tapeStop", action);
                action.setClickingTogglesState (true);
                knobs.push_back (makeKnob (apvts, *this, "tapeTime", "DUREE", "Temps de ralentissement jusqu'a l'arret"));
                break;
            case idg::fxStutter:
                action.setButtonText ("HOLD");
                action.setTooltip ("Repete en boucle le dernier fragment (longueur synchro tempo) tant que c'est actif");
                addAndMakeVisible (action);
                actionAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "stutterHold", action);
                action.setClickingTogglesState (true);
                combo.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T" }, 1);
                combo.setTooltip ("Longueur du fragment repete");
                addAndMakeVisible (combo);
                comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "stutterDiv", combo);
                knobs.push_back (makeKnob (apvts, *this, "stutterChance", "CHANCE", "Mode auto : probabilite de stutter a chaque temps"));
                break;
            case idg::fxGranular:
                knobs.push_back (makeKnob (apvts, *this, "grainDensity", "DENSITE", "Nombre de grains par seconde"));
                knobs.push_back (makeKnob (apvts, *this, "grainSize", "TAILLE", "Duree de chaque grain"));
                knobs.push_back (makeKnob (apvts, *this, "grainSpray", "DISPERSION", "Jusqu'ou dans le passe les grains vont piocher"));
                knobs.push_back (makeKnob (apvts, *this, "grainPitch", "PITCH RND", "Variation de hauteur aleatoire des grains"));
                break;
            default: break;
        }
    }

    bool isOn() const { return onToggle.getToggleState(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto head = r.removeFromTop (20);
        onToggle.setBounds (head.removeFromRight (50));
        r.removeFromTop (4);

        if (action.isVisible() || combo.isVisible())
        {
            auto row = r.removeFromTop (22);
            if (action.isVisible()) action.setBounds (row.removeFromLeft (combo.isVisible() ? row.getWidth() / 2 - 2 : row.getWidth()));
            if (combo.isVisible()) { row.removeFromLeft (4); combo.setBounds (row); }
            r.removeFromTop (6);
        }

        const int cols = 2;
        const int rows = ((int) knobs.size() + cols - 1) / cols;
        const int kh = juce::jmin (78, r.getHeight() / juce::jmax (1, rows));
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int c = (int) i % cols, rr = (int) i / cols;
            knobs[i]->setBounds ({ r.getX() + c * r.getWidth() / cols, r.getY() + rr * kh, r.getWidth() / cols, kh });
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto c = (id % 2 == 0) ? Theme::orange : Theme::blue;
        drawBox (g, getLocalBounds(), idg::fxModuleNames()[id], c, isOn());

        // Poignee de glisser-deposer
        g.setColour (Theme::textDim);
        for (int i = 0; i < 3; ++i)
            g.fillRect (getWidth() - 64 - 10, 7 + i * 4, 8, 1);

        // Voyant d'activite (tape stop / stutter)
        const bool active = (id == idg::fxTapeStop && owner.processor.getFxChain().tapeActive.load())
                         || (id == idg::fxStutter && owner.processor.getFxChain().stutterActive.load());
        if (active)
        {
            g.setColour (c);
            g.fillEllipse ((float) getWidth() - 88.0f, 6.0f, 8.0f, 8.0f);
        }

        if (dropHighlight)
        {
            g.setColour (Theme::text.withAlpha (0.15f));
            g.fillRect (getLocalBounds());
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.y < 24 && e.getDistanceFromDragStart() > 6)
            owner.startDragging (juce::var (id), this);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.isInt() && (int) d.description != id; }
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        owner.moveModule ((int) d.description, id);
    }

    void tick()
    {
        if (id == idg::fxTapeStop || id == idg::fxStutter) repaint (getLocalBounds().removeFromTop (20));
    }

private:
    FxView& owner;
    IDGemsSlicerProcessor& processor;
    const int id;
    juce::ToggleButton onToggle;
    juce::TextButton action;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, actionAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    std::vector<std::unique_ptr<FxView::Knob>> knobs;
    bool dropHighlight = false;
};

//==============================================================================
FxView::FxView (IDGemsSlicerProcessor& p) : processor (p)
{
    auto& apvts = processor.getAPVTS();
    for (int i = 0; i < idg::numFxModules; ++i)
    {
        cards[(size_t) i] = std::make_unique<ModuleCard> (*this, processor, i);
        addAndMakeVisible (*cards[(size_t) i]);
    }

    masterKnobs.push_back (makeKnob (apvts, *this, "fxSend", "ENVOI FX", "Part du son des slices envoyee dans la chaine FX "
                                                                          "(la lane ENVOI FX du sequenceur, si active, la remplace pas par pas)"));
    masterKnobs.push_back (makeKnob (apvts, *this, "ottDepth", "PROFONDEUR", "OTT : quantite de compression multibande (0 = off)"));
    masterKnobs.push_back (makeKnob (apvts, *this, "ottTime", "TEMPS", "OTT : vitesse de reaction"));
    masterKnobs.push_back (makeKnob (apvts, *this, "ottUp", "HAUT", "OTT : remonte les details faibles"));
    masterKnobs.push_back (makeKnob (apvts, *this, "ottDown", "BAS", "OTT : ecrase les pics forts"));
    masterKnobs.push_back (makeKnob (apvts, *this, "clipDrive", "DRIVE", "Soft clip : saturation douce"));
    masterKnobs.push_back (makeKnob (apvts, *this, "masterMix", "DRY/WET", "Dry/wet global de la chaine master (OTT + clip)"));
    masterKnobs.push_back (makeKnob (apvts, *this, "ceiling", "PLAFOND", "Niveau maximum en sortie (limiteur)"));
    masterKnobs.back()->slider.setNumDecimalPlacesToDisplay (1);

    clipToggle.setTooltip ("Active le soft clip");
    limiterToggle.setTooltip ("Limiteur de securite en fin de chaine");
    addAndMakeVisible (clipToggle);
    addAndMakeVisible (limiterToggle);
    clipAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "clipOn", clipToggle);
    limiterAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "limiterOn", limiterToggle);
}

FxView::~FxView() = default;

void FxView::moveModule (int moduleId, int targetModuleId)
{
    auto order = processor.getFxOrder();
    std::vector<int> v (order.begin(), order.end());
    v.erase (std::find (v.begin(), v.end(), moduleId));
    auto it = std::find (v.begin(), v.end(), targetModuleId);
    v.insert (it, moduleId);   // le module depose prend la place de la cible
    for (int i = 0; i < idg::numFxModules; ++i) order[(size_t) i] = v[(size_t) i];
    processor.setFxOrder (order);
    layoutCards();
    repaint();
}

void FxView::tick()
{
    for (auto& c : cards) c->tick();
    // L'ordre peut changer de l'exterieur (chargement d'un projet)
    static_assert (idg::numFxModules == 5, "");
}

void FxView::layoutCards()
{
    const auto order = processor.getFxOrder();
    const int gap = 8;
    const int w = (cardsArea.getWidth() - gap * (idg::numFxModules - 1)) / idg::numFxModules;
    for (int pos = 0; pos < idg::numFxModules; ++pos)
        cards[(size_t) order[(size_t) pos]]->setBounds (cardsArea.getX() + pos * (w + gap), cardsArea.getY(), w, cardsArea.getHeight());
}

void FxView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    masterArea = r.removeFromBottom (150);
    r.removeFromBottom (10);
    cardsArea = r.withTrimmedTop (14);
    layoutCards();

    auto m = masterArea.reduced (0, 18);
    m.removeFromTop (2);
    const int gap = 8;
    const int unit = (m.getWidth() - 4 * gap) / 10;
    sendArea = m.removeFromLeft (unit);           m.removeFromLeft (gap);
    ottArea = m.removeFromLeft (unit * 4);        m.removeFromLeft (gap);
    clipArea = m.removeFromLeft (unit * 2 - 10);  m.removeFromLeft (gap);
    mixArea = m.removeFromLeft (unit);            m.removeFromLeft (gap);
    limitArea = m;

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (4, 4).withTrimmedTop (18); };
    masterKnobs[0]->setBounds (inner (sendArea));
    {
        auto a = inner (ottArea);
        const int w = a.getWidth() / 4;
        for (int i = 0; i < 4; ++i) masterKnobs[(size_t) (1 + i)]->setBounds (a.removeFromLeft (w));
    }
    {
        auto a = inner (clipArea);
        clipToggle.setBounds (clipArea.reduced (6, 4).removeFromTop (18).removeFromRight (100));
        masterKnobs[5]->setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), 90), a.getHeight()));
    }
    masterKnobs[6]->setBounds (inner (mixArea));
    {
        auto a = inner (limitArea);
        limiterToggle.setBounds (limitArea.reduced (6, 4).removeFromTop (18).removeFromRight (100));
        masterKnobs[7]->setBounds (a.withSizeKeepingCentre (juce::jmin (a.getWidth(), 90), a.getHeight()));
    }
}

void FxView::paint (juce::Graphics& g)
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
    g.drawText ("CHAINE FX IDGEMS", r.removeFromTop (20), juce::Justification::centredLeft);

    g.setFont (IDGemsLookAndFeel::monoFont (11.0f));
    g.setColour (Theme::textDim);
    g.drawText ("Glissez le titre d'un module sur un autre pour changer l'ordre  >  traitement de gauche a droite",
                cardsArea.withHeight (14).translated (0, -14), juce::Justification::centredLeft);

    g.setColour (Theme::blue);
    g.setFont (IDGemsLookAndFeel::titleFont (13.0f));
    g.drawText ("MASTER  //  OTT > SOFT CLIP > LIMITEUR", masterArea.removeFromTop (16), juce::Justification::centredLeft);

    drawBox (g, sendArea, "ENVOI", Theme::orange);
    drawBox (g, ottArea, "OTT", Theme::blue);
    drawBox (g, clipArea, "CLIP", Theme::orange);
    drawBox (g, mixArea, "MIX", Theme::blue);
    drawBox (g, limitArea, "LIMIT", Theme::orange);
}
