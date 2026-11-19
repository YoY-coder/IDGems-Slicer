#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class IDGemsSlicerProcessor;

//==============================================================================
// Onglet RANDOM : random general / par section, dosage, mode, seed,
// historique, cadenas, plages par parametre, mutate auto, morph pad.
//==============================================================================
class RandomView final : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit RandomView (IDGemsSlicerProcessor&);
    ~RandomView() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    // Bouton cadenas (dessine)
    class LockButton final : public juce::Button
    {
    public:
        LockButton() : juce::Button ("lock") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

private:
    class ParamList;
    class MorphPad;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void selectSection (int);
    void refresh();

    IDGemsSlicerProcessor& processor;

    juce::TextButton randomAllButton { "RANDOM GENERAL" };
    juce::Slider amountSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ComboBox modeBox;
    juce::Label amountLabel, modeLabel, seedLabel, historyLabel;
    juce::TextEditor seedEditor;
    juce::ToggleButton fixedSeedToggle { "FIXE" };
    juce::TextButton backButton { "< RETOUR" }, forwardButton { "AVANT >" };

    struct SectionRow
    {
        LockButton lock;
        juce::TextButton name, random { "RND" };
    };
    std::array<std::unique_ptr<SectionRow>, 6> sectionRows;
    int selectedSection = 0;

    juce::ToggleButton mutateToggle { "MUTATE AUTO" };
    juce::Slider mutateBars { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Slider mutateRate { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label mutateBarsLabel, mutateRateLabel;

    std::unique_ptr<ParamList> paramList;
    juce::Viewport paramViewport;

    std::unique_ptr<MorphPad> morphPad;
    juce::ToggleButton morphToggle { "MORPH ACTIF" };
    std::array<juce::TextButton, 4> captureButtons;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt, mutateBarsAtt, mutateRateAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mutateAtt, morphAtt;

    juce::Rectangle<int> leftArea, middleArea, rightArea, sectionsHeader, mutateArea;
};
