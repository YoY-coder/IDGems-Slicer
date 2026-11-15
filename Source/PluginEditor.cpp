#include "PluginEditor.h"
#include "UI/Theme.h"

namespace
{
    constexpr double minScale = 0.6;
    constexpr double maxScale = 2.0;
}

IDGemsSlicerEditor::IDGemsSlicerEditor (IDGemsSlicerProcessor& p)
    : AudioProcessorEditor (&p), processor (p), view (p)
{
    // Lire la taille memorisee AVANT de regler les limites (qui redimensionnent)
    const auto saved = processor.getSavedEditorSize();

    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    constexpr int bw = IDGemsSlicerProcessor::baseWidth;
    constexpr int bh = IDGemsSlicerProcessor::baseHeight;

    setResizable (true, true);
    setResizeLimits ((int) (bw * minScale), (int) (bh * minScale),
                     (int) (bw * maxScale), (int) (bh * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) bw / (double) bh);

    processor.setSavedEditorSize (saved.x, saved.y);
    restoreSavedSize();
    sizeRestored = true;
}

IDGemsSlicerEditor::~IDGemsSlicerEditor()
{
    setLookAndFeel (nullptr);
}

void IDGemsSlicerEditor::restoreSavedSize()
{
    const auto s = processor.getSavedEditorSize();
    constexpr int bw = IDGemsSlicerProcessor::baseWidth;
    const double scale = juce::jlimit (minScale, maxScale, (double) s.x / bw);
    setSize (juce::roundToInt (bw * scale),
             juce::roundToInt (IDGemsSlicerProcessor::baseHeight * scale));
}

void IDGemsSlicerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void IDGemsSlicerEditor::resized()
{
    const float scale = (float) getWidth() / (float) IDGemsSlicerProcessor::baseWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, IDGemsSlicerProcessor::baseWidth, IDGemsSlicerProcessor::baseHeight);

    if (sizeRestored)
        processor.setSavedEditorSize (getWidth(), getHeight());
}
