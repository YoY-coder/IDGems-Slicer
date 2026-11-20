#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class IDGemsSlicerProcessor;

//==============================================================================
// Barre d'en-tete : presets (precedent / liste / suivant / sauver), banque de
// 8 patterns (changeables au MIDI), undo / redo.
//==============================================================================
class HeaderBar final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit HeaderBar (IDGemsSlicerProcessor&);
    ~HeaderBar() override;

    void tick();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onPresetLoaded;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPresetMenu();
    void loadPreset (const juce::File&);
    void savePreset();
    void showPatternMenu (int index);
    void showInitMenu();

    IDGemsSlicerProcessor& processor;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, nameButton, saveButton { "SAUVER" }, initButton { "INIT" };
    juce::ToggleButton embedToggle { "+ SAMPLES" };
    std::array<juce::TextButton, 8> patternButtons;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> patternLabelArea;
    int shownPattern = -1;
};
