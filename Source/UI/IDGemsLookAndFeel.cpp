#include "IDGemsLookAndFeel.h"
#include "Theme.h"

IDGemsLookAndFeel::IDGemsLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
    setColour (juce::Slider::textBoxTextColourId,        Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,  juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,                Theme::text);
    setColour (juce::TooltipWindow::backgroundColourId,  Theme::panel.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId,        Theme::text);
    setColour (juce::TooltipWindow::outlineColourId,     Theme::orange);
}

juce::Font IDGemsLookAndFeel::monoFont (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::Font IDGemsLookAndFeel::getLabelFont (juce::Label& l)
{
    return monoFont (juce::jmin (14.0f, (float) l.getHeight() * 0.8f));
}

void IDGemsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float startAngle, float endAngle,
                                          juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (2.0f, radius * 0.16f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - thick, radius - thick, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::panelBorder);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - thick, radius - thick, 0.0f, startAngle, angle, true);

    // Aberration chromatique legere : un halo bleu decale sous l'arc orange
    g.setColour (Theme::blue.withAlpha (0.45f));
    g.saveState();
    g.addTransform (juce::AffineTransform::translation (-1.2f, 0.6f));
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.restoreState();

    g.setColour (Theme::orange);
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // Aiguille
    const auto tip = centre.getPointOnCircumference (radius - thick * 2.2f, angle);
    g.setColour (Theme::text);
    g.drawLine ({ centre, tip }, juce::jmax (1.5f, thick * 0.5f));
}

void IDGemsLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    const auto col = (over || dragging) ? Theme::orange : Theme::textDim;
    g.setColour (col);
    const float s = (float) juce::jmin (w, h);
    for (int i = 1; i <= 3; ++i)
    {
        const float o = s * (float) i / 4.0f;
        g.drawLine ((float) w - o, (float) h - 1.0f, (float) w - 1.0f, (float) h - o, 1.5f);
    }
}
