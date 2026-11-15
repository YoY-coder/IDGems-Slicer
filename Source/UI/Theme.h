#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
// Direction artistique IDGems : noir texture, orange, touches de bleu.
// Toutes les couleurs du plugin passent par ici.
//==============================================================================
namespace Theme
{
    // Fonds
    inline const juce::Colour background   { 0xff0a0a0c };
    inline const juce::Colour panel        { 0xff121216 };
    inline const juce::Colour panelBorder  { 0xff2a2a30 };

    // Accents
    inline const juce::Colour orange       { 0xffff6a1a };
    inline const juce::Colour blue         { 0xff2e9bff };

    // Texte
    inline const juce::Colour text         { 0xffe8e6e3 };
    inline const juce::Colour textDim      { 0xff8a8890 };

    // Couleurs des 4 samples (lisibles sur noir, bien distinctes)
    inline const juce::Colour sampleColours[4] =
    {
        juce::Colour (0xffff7a1a),   // 1 : orange
        juce::Colour (0xff27c6ff),   // 2 : bleu cyan
        juce::Colour (0xffff2e88),   // 3 : magenta
        juce::Colour (0xffb8ff3b),   // 4 : vert acide
    };

    inline juce::Colour sampleColour (int index) { return sampleColours[juce::jlimit (0, 3, index)]; }
}
