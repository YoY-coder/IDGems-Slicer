#include "WaveformView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include <algorithm>

WaveformView::WaveformView (bool canEdit) : editable (canEdit)
{
    setRepaintsOnMouseActivity (false);
}

void WaveformView::setData (idg::SampleData::Ptr d, juce::Colour c)
{
    const bool newAudio = d == nullptr || data == nullptr || d->audio != data->audio;
    data = d;
    colour = c;
    cuts.clear();
    if (data != nullptr)
        for (auto& s : data->slices) cuts.push_back (s.start);

    if (newAudio)
    {
        viewStart = 0.0;
        viewEnd = data != nullptr ? (double) data->audio->getLength() : 1.0;
        selectedSlice = -1;
    }
    dragMarker = -1;
    repaint();
}

void WaveformView::setPlayheads (const std::vector<float>& p)
{
    if (p != playheads) { playheads = p; repaint(); }
}

void WaveformView::flashSlice (int slice)
{
    flashIndex = slice;
    flashAmount = 1.0f;
}

void WaveformView::tick()
{
    if (flashAmount > 0.0f)
    {
        flashAmount = juce::jmax (0.0f, flashAmount - 0.08f);
        repaint();
    }
}

double WaveformView::sampleToX (double s) const
{
    return (s - viewStart) / juce::jmax (1.0, viewEnd - viewStart) * getWidth();
}

double WaveformView::xToSample (double x) const
{
    return viewStart + x / juce::jmax (1, getWidth()) * (viewEnd - viewStart);
}

int WaveformView::markerAt (float x) const
{
    int best = -1;
    float bestD = 5.0f;
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const float d = std::abs ((float) sampleToX (cuts[i]) - x);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

int WaveformView::sliceAt (double sample) const
{
    if (cuts.empty() || sample < cuts.front()) return -1;
    auto it = std::upper_bound (cuts.begin(), cuts.end(), (int) sample);
    return (int) std::distance (cuts.begin(), it) - 1;
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (r);

    if (data == nullptr)
    {
        g.setColour (Theme::panelBorder);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        return;
    }

    const auto& audio = *data->audio;
    const float mid = r.getCentreY();
    const float halfH = r.getHeight() * 0.46f;

    // Fond alterne des slices + flash de la slice jouee
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const double s0 = cuts[i];
        const double s1 = i + 1 < cuts.size() ? cuts[i + 1] : audio.getLength();
        const float x0 = (float) sampleToX (s0), x1 = (float) sampleToX (s1);
        if (x1 < 0 || x0 > r.getRight()) continue;

        float alpha = (i % 2 == 0) ? 0.06f : 0.0f;
        if ((int) i == selectedSlice && editable) alpha = 0.16f;
        if ((int) i == flashIndex) alpha = juce::jmax (alpha, 0.35f * flashAmount);
        if (alpha > 0.0f)
        {
            g.setColour (colour.withAlpha (alpha));
            g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        }
    }

    // Forme d'onde a partir de l'apercu (crete par bloc)
    const auto& peaks = audio.blockPeaks;
    if (! peaks.empty())
    {
        juce::Path wave;
        const int w = getWidth();
        std::vector<float> col ((size_t) w, 0.0f);
        for (int x = 0; x < w; ++x)
        {
            const double a = xToSample (x), b = xToSample (x + 1);
            const int b0 = juce::jlimit (0, (int) peaks.size() - 1, (int) (a / idg::peakBlockSize));
            const int b1 = juce::jlimit (0, (int) peaks.size() - 1, (int) (b / idg::peakBlockSize));
            float p = 0.0f;
            for (int k = b0; k <= b1; ++k) p = juce::jmax (p, peaks[(size_t) k]);
            col[(size_t) x] = juce::jmin (1.0f, p);
        }
        wave.startNewSubPath (0.0f, mid);
        for (int x = 0; x < w; ++x) wave.lineTo ((float) x, mid - col[(size_t) x] * halfH);
        for (int x = w - 1; x >= 0; --x) wave.lineTo ((float) x, mid + col[(size_t) x] * halfH);
        wave.closeSubPath();

        // Aberration chromatique discrete
        g.setColour (Theme::blue.withAlpha (0.25f));
        g.fillPath (wave, juce::AffineTransform::translation (-1.5f, 0.0f));
        g.setColour (colour.withAlpha (0.85f));
        g.fillPath (wave);
    }

    // Marqueurs
    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const float x = (float) sampleToX (cuts[i]);
        if (x < -1 || x > r.getRight() + 1) continue;
        const bool hot = (int) i == hoverMarker || (int) i == dragMarker;
        g.setColour (hot ? Theme::text : colour.withAlpha (editable ? 0.75f : 0.45f));
        g.fillRect (x - (hot ? 1.0f : 0.5f), r.getY(), hot ? 2.0f : 1.0f, r.getHeight());
        if (editable)
        {
            juce::Path tab;
            tab.addTriangle (x - 4.0f, r.getY(), x + 4.0f, r.getY(), x, r.getY() + 6.0f);
            g.fillPath (tab);
        }
    }

    // Tetes de lecture
    for (auto p : playheads)
    {
        const float x = (float) sampleToX (p * audio.getLength());
        g.setColour (Theme::text);
        g.fillRect (x - 0.5f, r.getY(), 1.5f, r.getHeight());
    }

    // Indicateur de zoom
    if (editable && (viewStart > 0.0 || viewEnd < audio.getLength()))
    {
        const float total = (float) audio.getLength();
        auto bar = r.removeFromBottom (3.0f);
        g.setColour (Theme::panelBorder);
        g.fillRect (bar);
        g.setColour (Theme::orange);
        g.fillRect (bar.getX() + bar.getWidth() * (float) viewStart / total, bar.getY(),
                    bar.getWidth() * (float) (viewEnd - viewStart) / total, bar.getHeight());
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (! editable || data == nullptr) return;
    const int m = markerAt ((float) e.x);
    if (m != hoverMarker)
    {
        hoverMarker = m;
        setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoverMarker != -1) { hoverMarker = -1; repaint(); }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (onClick) onClick();
    if (data == nullptr) return;

    dragged = false;
    if (editable)
    {
        const int m = markerAt ((float) e.x);
        if (m >= 0 && e.mods.isPopupMenu())
        {
            if (cuts.size() > 1)
            {
                cuts.erase (cuts.begin() + m);
                hoverMarker = -1;
                commitCuts();
            }
            return;
        }
        if (m >= 0)
        {
            dragMarker = m;
            return;
        }
    }

    // Clic dans une slice : pre-ecoute
    const int s = sliceAt (xToSample (e.x));
    if (s >= 0)
    {
        selectedSlice = s;
        if (onPreview) onPreview (s);
        repaint();
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker < 0 || data == nullptr) return;
    dragged = true;
    const int len = data->audio->getLength();
    const int lo = dragMarker > 0 ? cuts[(size_t) dragMarker - 1] + 64 : 0;
    const int hi = dragMarker + 1 < (int) cuts.size() ? cuts[(size_t) dragMarker + 1] - 64 : len - 64;
    cuts[(size_t) dragMarker] = juce::jlimit (lo, juce::jmax (lo, hi), (int) xToSample (e.x));
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragMarker >= 0 && dragged)
        commitCuts();
    dragMarker = -1;
    dragged = false;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable || data == nullptr || markerAt ((float) e.x) >= 0) return;
    if ((int) cuts.size() >= idg::maxSlices) return;
    const int pos = (int) xToSample (e.x);
    cuts.insert (std::upper_bound (cuts.begin(), cuts.end(), pos), pos);
    commitCuts();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! editable || data == nullptr) return;
    const double total = data->audio->getLength();
    double span = viewEnd - viewStart;

    if (std::abs (w.deltaX) > std::abs (w.deltaY) || e.mods.isShiftDown())
    {
        // Defilement horizontal
        const double d = (std::abs (w.deltaX) > 0 ? -w.deltaX : -w.deltaY) * span * 0.5;
        viewStart = juce::jlimit (0.0, total - span, viewStart + d);
        viewEnd = viewStart + span;
    }
    else
    {
        // Zoom autour de la souris
        const double anchor = xToSample (e.x);
        const double factor = std::pow (0.8, w.deltaY * 4.0);
        span = juce::jlimit (juce::jmin (total, 2048.0), total, span * factor);
        const double ratio = (anchor - viewStart) / juce::jmax (1.0, viewEnd - viewStart);
        viewStart = juce::jlimit (0.0, total - span, anchor - ratio * span);
        viewEnd = viewStart + span;
    }
    repaint();
}

void WaveformView::commitCuts()
{
    repaint();
    if (onCutsChanged) onCutsChanged (cuts);
}
