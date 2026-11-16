#include "IDGemsLookAndFeel.h"
#include "Theme.h"

#if IDGEMS_HAS_CUSTOM_FONT
 #include "BinaryData.h"
#endif

namespace
{
    juce::Typeface::Ptr loadCustomTitleTypeface()
    {
       #if IDGEMS_HAS_CUSTOM_FONT
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const auto* name = BinaryData::namedResourceList[i];
            const juce::String file (BinaryData::getNamedResourceOriginalFilename (name));
            if (file.endsWithIgnoreCase (".ttf") || file.endsWithIgnoreCase (".otf"))
            {
                int size = 0;
                if (const auto* data = BinaryData::getNamedResource (name, size))
                    if (auto tf = juce::Typeface::createSystemTypefaceFor (data, (size_t) size))
                        return tf;
            }
        }
       #endif
        return nullptr;
    }

    juce::Typeface::Ptr getCustomTitleTypeface()
    {
        static juce::Typeface::Ptr tf = loadCustomTitleTypeface();   // charge une seule fois
        return tf;
    }
}

bool IDGemsLookAndFeel::hasCustomTitleFont()
{
    return getCustomTitleTypeface() != nullptr;
}

juce::Font IDGemsLookAndFeel::titleFont (float height)
{
    if (auto tf = getCustomTitleTypeface())
        return juce::Font (juce::FontOptions (tf).withHeight (height));

    return monoFont (height).boldened();
}

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

    setColour (juce::ComboBox::backgroundColourId,       Theme::panel.brighter (0.05f));
    setColour (juce::ComboBox::outlineColourId,          Theme::panelBorder);
    setColour (juce::ComboBox::textColourId,             Theme::text);
    setColour (juce::ComboBox::arrowColourId,            Theme::orange);
    setColour (juce::PopupMenu::backgroundColourId,      Theme::panel);
    setColour (juce::PopupMenu::textColourId,            Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::orange.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId,         Theme::panel.brighter (0.05f));
    setColour (juce::TextButton::buttonOnColourId,       Theme::orange);
    setColour (juce::TextButton::textColourOffId,        Theme::text);
    setColour (juce::TextButton::textColourOnId,         juce::Colours::black);
    setColour (juce::ToggleButton::textColourId,         Theme::text);
    setColour (juce::ToggleButton::tickColourId,         Theme::orange);
    setColour (juce::Slider::trackColourId,              Theme::orange);
    setColour (juce::Slider::backgroundColourId,         Theme::panelBorder);
    setColour (juce::Slider::thumbColourId,              Theme::text);
    setColour (juce::TextEditor::backgroundColourId,     Theme::panel);
    setColour (juce::TextEditor::textColourId,           Theme::text);
    setColour (juce::TextEditor::highlightColourId,      Theme::orange.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId,        Theme::panelBorder);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::orange);
    setColour (juce::CaretComponent::caretColourId,      Theme::orange);
}

juce::Font IDGemsLookAndFeel::getComboBoxFont (juce::ComboBox& c)
{
    return monoFont (juce::jmin (14.0f, (float) c.getHeight() * 0.6f));
}

juce::Font IDGemsLookAndFeel::getPopupMenuFont()
{
    return monoFont (15.0f);
}

juce::Font IDGemsLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return monoFont (juce::jmin (13.0f, (float) h * 0.6f)).boldened();
}

void IDGemsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                              bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) fill = fill.brighter (0.2f);
    g.setColour (fill);
    g.fillRect (r);
    g.setColour (over ? Theme::orange : Theme::panelBorder.brighter (0.2f));
    g.drawRect (r, 1.0f);
}

void IDGemsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    g.setColour (Theme::panel);
    g.fillRect (box);
    g.setColour (over ? Theme::orange : Theme::panelBorder.brighter (0.3f));
    g.drawRect (box, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (b.findColour (juce::ToggleButton::tickColourId));
        g.fillRect (box.reduced (3.0f));
    }
    g.setColour (b.isEnabled() ? Theme::text : Theme::textDim);
    g.setFont (monoFont (juce::jmin (13.0f, r.getHeight() * 0.6f)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

void IDGemsLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (c.findColour (juce::ComboBox::backgroundColourId));
    g.fillRect (r);
    g.setColour (c.isMouseOver (true) ? Theme::orange : c.findColour (juce::ComboBox::outlineColourId));
    g.drawRect (r, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (c.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void IDGemsLookAndFeel::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (4, 1, c.getWidth() - 22, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}

void IDGemsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float minPos, float maxPos,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto accent = s.findColour (juce::Slider::trackColourId);

    if (style == juce::Slider::LinearBar)
    {
        g.setColour (Theme::panel);
        g.fillRect (r);
        auto fill = r.withRight (pos);
        g.setColour (accent.withAlpha (0.55f));
        g.fillRect (fill);
        g.setColour (accent);
        g.fillRect (fill.withLeft (juce::jmax (fill.getX(), fill.getRight() - 2.0f)));
        g.setColour (Theme::panelBorder);
        g.drawRect (r, 1.0f);
        return;
    }

    // Horizontal : piste fine + curseur rectangulaire
    auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    g.setColour (Theme::panelBorder);
    g.fillRect (track);
    g.setColour (accent);
    g.fillRect (track.withRight (pos));
    g.setColour (Theme::text);
    g.fillRect (juce::Rectangle<float> (6.0f, juce::jmin (16.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() }));
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
