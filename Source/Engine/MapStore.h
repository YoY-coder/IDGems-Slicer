#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>

namespace idg
{
//==============================================================================
// Trajet dessine sur la carte des slices (coordonnees 0..1). Edite dans
// l'interface, copie vers le thread audio sans allocation ni blocage.
//==============================================================================
constexpr int maxPathPoints = 256;

struct MapPath
{
    std::array<juce::Point<float>, maxPathPoints> points {};
    int count = 0;

    // Position sur le trajet, u de 0 a 1 (a vitesse constante le long du trait)
    juce::Point<float> at (float u) const noexcept;
    float totalLength() const noexcept;
};

const juce::StringArray& pathLengthNames();    // duree d'un parcours complet
double pathLengthInBeats (int index);
const juce::StringArray& pathModeNames();

class MapStore
{
public:
    const MapPath& get() const noexcept { return edit; }
    void setPath (const MapPath&);
    void clear();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    void pull (MapPath& dest, int& lastVersion) noexcept;

private:
    void publish();
    MapPath edit, shared;
    mutable juce::SpinLock lock;
    std::atomic<int> version { 1 };
};
}
