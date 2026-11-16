#include "SlotPanel.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"

SlotPanel::SlotPanel (IDGemsSlicerProcessor& p, int index)
    : processor (p), slot (index), colour (Theme::sampleColour (index))
{
    addAndMakeVisible (wave);
    wave.setInterceptsMouseClicks (false, false);

    loadButton.setTooltip ("Charger un fichier audio (WAV, AIFF, FLAC...). Vous pouvez aussi glisser-deposer un fichier sur la carte.");
    loadButton.onClick = [this] { browse(); };
    addAndMakeVisible (loadButton);

    clearButton.setTooltip ("Vider cet emplacement");
    clearButton.onClick = [this] { processor.getSampleManager().clearSlot (slot); };
    addAndMakeVisible (clearButton);

    prob.setColour (juce::Slider::trackColourId, colour);
    prob.setTextValueSuffix (" %");
    prob.setNumDecimalPlacesToDisplay (0);
    prob.setTooltip ("Probabilite de piocher dans ce sample a chaque declenchement. "
                     "Les 4 valeurs sont normalisees automatiquement (chance reelle a droite).");
    prob.setDoubleClickReturnValue (true, 100.0);
    addAndMakeVisible (prob);
    probAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), "prob" + juce::String (slot + 1), prob);

    probLabel.setJustificationType (juce::Justification::centredRight);
    probLabel.setFont (IDGemsLookAndFeel::monoFont (13.0f));
    probLabel.setTooltip ("Chance reelle (apres normalisation, emplacements vides ignores)");
    addAndMakeVisible (probLabel);

    refresh();
}

SlotPanel::~SlotPanel() = default;

void SlotPanel::refresh()
{
    const auto info = processor.getSampleManager().getSlot (slot);
    loaded = info.data != nullptr;
    wave.setData (info.data, colour);

    title = "SAMPLE " + juce::String (slot + 1);
    if (info.loading)
        status = "Chargement...";
    else if (info.error.isNotEmpty() && ! loaded)
        status = info.error + " : " + info.file.getFileName();
    else if (loaded)
        status = info.data->audio->name + "  |  " + juce::String (info.data->numSlices()) + " slices"
                 + (info.error.isNotEmpty() ? "  (" + info.error + ")" : juce::String());
    else
        status = "Glisser un fichier ici";

    clearButton.setEnabled (loaded || info.file != juce::File());
    prob.setAlpha (loaded ? 1.0f : 0.45f);
    repaint();
}

void SlotPanel::tick (float normalisedProb)
{
    if (std::abs (normalisedProb - realProb) > 0.0005f || probLabel.getText().isEmpty())
    {
        realProb = normalisedProb;
        probLabel.setText (loaded ? juce::String (juce::roundToInt (realProb * 100.0f)) + " %" : "--",
                           juce::dontSendNotification);
        probLabel.setColour (juce::Label::textColourId, loaded ? colour : Theme::textDim);
    }

    // Tetes de lecture des voix qui jouent ce sample
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

void SlotPanel::setSelected (bool s)
{
    if (selected != s) { selected = s; repaint(); }
}

//==============================================================================
void SlotPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (20);
    clearButton.setBounds (top.removeFromRight (22));
    top.removeFromRight (4);
    loadButton.setBounds (top.removeFromRight (78));

    r.removeFromTop (20);   // ligne de statut
    auto bottom = r.removeFromBottom (20);
    probLabel.setBounds (bottom.removeFromRight (52));
    bottom.removeFromRight (4);
    prob.setBounds (bottom);
    r.removeFromBottom (6);
    wave.setBounds (r);
}

void SlotPanel::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.9f));
    g.fillRect (a);
    g.setColour (selected ? colour : Theme::panelBorder);
    g.drawRect (a, selected ? 1.5f : 1.0f);

    juce::Path corner;
    corner.startNewSubPath (a.getX(), a.getY() + 18.0f);
    corner.lineTo (a.getX(), a.getY());
    corner.lineTo (a.getX() + 18.0f, a.getY());
    g.setColour (colour);
    g.strokePath (corner, juce::PathStrokeType (2.0f));

    auto r = getLocalBounds().reduced (10, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (colour);
    g.drawText (title, r.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (IDGemsLookAndFeel::monoFont (12.0f));
    g.setColour (loaded ? Theme::text : Theme::textDim);
    g.drawFittedText (status, r.removeFromTop (20), juce::Justification::centredLeft, 1, 0.9f);
}

void SlotPanel::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (colour.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (colour);
        g.drawRect (getLocalBounds(), 2);
    }
}

void SlotPanel::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (slot);
}

//==============================================================================
bool SlotPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (idg::SampleManager::isSupportedFile (juce::File (f))) return true;
    return false;
}

void SlotPanel::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint(); }
void SlotPanel::fileDragExit (const juce::StringArray&)            { dragOver = false; repaint(); }

void SlotPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
        if (idg::SampleManager::isSupportedFile (juce::File (f)))
        {
            loadInto (juce::File (f));
            break;
        }
}

void SlotPanel::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Charger un sample dans l'emplacement " + juce::String (slot + 1),
                                                   juce::File(), idg::SampleManager::supportedWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadInto (f);
                          });
}

void SlotPanel::loadInto (const juce::File& f)
{
    if (onSelect) onSelect (slot);
    processor.getSampleManager().loadFile (slot, f);
}
