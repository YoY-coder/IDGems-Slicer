#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Engine/FxChain.h"

class IDGemsSlicerProcessor;

//==============================================================================
// Chaine FX IDGems (5 modules, ordre par glisser-deposer) + chaine master.
//==============================================================================
class FxView final : public juce::Component,
                     public juce::DragAndDropContainer
{
public:
    explicit FxView (IDGemsSlicerProcessor&);
    ~FxView() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    // Petit bouton rotatif avec etiquette (partage avec les cartes)
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        void setBounds (juce::Rectangle<int>);
    };

    class ModuleCard;

private:
    void layoutCards();
    void moveModule (int moduleId, int targetModuleId);

    IDGemsSlicerProcessor& processor;
    std::array<std::unique_ptr<ModuleCard>, idg::numFxModules> cards;
    std::vector<std::unique_ptr<Knob>> masterKnobs;
    juce::ToggleButton clipToggle { "SOFT CLIP" }, limiterToggle { "LIMITEUR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clipAtt, limiterAtt;
    juce::Rectangle<int> cardsArea, masterArea, sendArea, ottArea, clipArea, mixArea, limitArea;

    friend class ModuleCard;
};
