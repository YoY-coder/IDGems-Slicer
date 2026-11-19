#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SlotPanel.h"
#include "SliceEditor.h"
#include "PlayPanel.h"
#include "SequencerView.h"
#include "MapView.h"
#include "VoiceView.h"
#include "FxView.h"
#include "RandomView.h"
#include "ExportView.h"
#include "HeaderBar.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Vue principale, dessinee a une taille de reference fixe (1280 x 800).
// L'editeur la met a l'echelle avec une transformation : tout reste net
// (vectoriel) quelle que soit la taille de la fenetre.
//==============================================================================
class MainView final : public juce::Component,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit MainView (IDGemsSlicerProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void selectSlot (int);
    void selectTab (int);
    void drawGlitchTitle (juce::Graphics&, juce::Rectangle<float> area);
    void drawPlaceholder (juce::Graphics&, juce::Rectangle<int> area,
                          const juce::String& title, const juce::String& note,
                          juce::Colour accent);
    void drawMeter (juce::Graphics&, juce::Rectangle<float> area);
    void drawGutterGlitch (juce::Graphics&);
    juce::RectangleList<int> gutters() const;
    float gutterGlitch = 0.0f;
    void buildTexture();

    IDGemsSlicerProcessor& processor;

    juce::Image texture;

    juce::Slider outputGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  outputGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    std::array<std::unique_ptr<SlotPanel>, 4> slotPanels;
    SliceEditor sliceEditor;
    SequencerView sequencerView;
    MapView mapView;
    VoiceView voiceView;
    FxView fxView;
    RandomView randomView;
    ExportView exportView;
    HeaderBar headerBar;
    PlayPanel playPanel;
    juce::TextButton cutTab { "DECOUPE" }, seqTab { "SEQUENCEUR" }, mapTab { "CARTE" }, voiceTab { "VOIX" }, fxTab { "FX" }, randomTab { "RANDOM" }, exportTab { "EXPORT" };
    int currentTab = 0;
    juce::Rectangle<int> tabArea;

    // Zones (coordonnees de reference)
    juce::Rectangle<int> headerArea, slotsArea, mapArea, fxArea, seqArea, meterArea, midiLedArea;

    float meterL = 0.0f, meterR = 0.0f;
    int   lastMidiCount = 0;
    float midiFlash = 0.0f;
    float glitchAmount = 0.0f;
    juce::Random rng;
    int   glitchSeed = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
