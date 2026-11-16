#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Engine/SampleData.h"
#include <functional>

//==============================================================================
// Forme d'onde d'un sample avec ses slices.
//  - mode "compact" : apercu dans les cartes d'emplacement (pas d'edition)
//  - mode "editeur" : clic = pre-ecoute, double-clic = ajouter un marqueur,
//    glisser un marqueur = deplacer, clic droit = supprimer, molette = zoom.
//==============================================================================
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (bool editable);

    void setData (idg::SampleData::Ptr, juce::Colour);
    void setPlayheads (const std::vector<float>& positions);   // 0..1
    void flashSlice (int slice);
    void tick();   // appele ~30 fois / s

    std::function<void (int slice)> onPreview;
    std::function<void (std::vector<int> cuts)> onCutsChanged;
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    double sampleToX (double s) const;
    double xToSample (double x) const;
    int markerAt (float x) const;       // index du point de coupe sous la souris, ou -1
    int sliceAt (double sample) const;
    void commitCuts();

    const bool editable;
    idg::SampleData::Ptr data;
    juce::Colour colour;
    std::vector<int> cuts;              // copie locale des debuts de slices (edition)
    std::vector<float> playheads;

    double viewStart = 0.0, viewEnd = 1.0;   // en samples
    int dragMarker = -1, hoverMarker = -1, selectedSlice = -1;
    bool dragged = false;
    int flashIndex = -1;
    float flashAmount = 0.0f;
};
