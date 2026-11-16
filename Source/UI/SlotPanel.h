#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "WaveformView.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Carte d'un emplacement de sample : nom, apercu, probabilite, charger / vider.
// Accepte le glisser-deposer de fichiers audio.
//==============================================================================
class SlotPanel final : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    SlotPanel (IDGemsSlicerProcessor&, int slotIndex);
    ~SlotPanel() override;

    void refresh();                       // relit l'etat de l'emplacement
    void tick (float normalisedProb);     // ~30 fois / s
    void setSelected (bool);

    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void browse();
    void loadInto (const juce::File&);

    IDGemsSlicerProcessor& processor;
    const int slot;
    const juce::Colour colour;

    WaveformView wave { false };
    juce::TextButton loadButton { "CHARGER" }, clearButton { "X" };
    juce::Slider prob { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label probLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> probAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String title, status;
    bool selected = false, dragOver = false, loaded = false;
    float realProb = 0.0f;
    int lastTriggerCount = 0;
};
