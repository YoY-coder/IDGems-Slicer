#include "MapStore.h"

namespace idg
{
namespace IDs
{
    static const juce::Identifier mapPath { "MapPath" };
    static const juce::Identifier points  { "points" };
}

float MapPath::totalLength() const noexcept
{
    float len = 0.0f;
    for (int i = 1; i < count; ++i) len += points[(size_t) i].getDistanceFrom (points[(size_t) i - 1]);
    return len;
}

juce::Point<float> MapPath::at (float u) const noexcept
{
    if (count <= 0) return { 0.5f, 0.5f };
    if (count == 1) return points[0];

    const float total = totalLength();
    if (total <= 1.0e-6f) return points[0];

    float target = juce::jlimit (0.0f, 1.0f, u) * total;
    for (int i = 1; i < count; ++i)
    {
        const auto a = points[(size_t) i - 1], b = points[(size_t) i];
        const float seg = a.getDistanceFrom (b);
        if (target <= seg || i == count - 1)
            return seg > 0.0f ? a + (b - a) * juce::jmin (1.0f, target / seg) : a;
        target -= seg;
    }
    return points[(size_t) count - 1];
}

const juce::StringArray& pathLengthNames()
{
    static const juce::StringArray n { "1/4 mesure", "1/2 mesure", "1 mesure", "2 mesures", "4 mesures", "8 mesures", "16 mesures" };
    return n;
}

double pathLengthInBeats (int i)
{
    static const double b[] { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
    return b[juce::jlimit (0, 6, i)];
}

const juce::StringArray& pathModeNames()
{
    static const juce::StringArray n { "Aller simple", "Ping-pong", "Random" };
    return n;
}

void MapStore::publish()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        shared = edit;
    }
    version.fetch_add (1);
}

void MapStore::pull (MapPath& dest, int& lastVersion) noexcept
{
    const int v = version.load();
    if (v == lastVersion) return;
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked())
    {
        dest = shared;
        lastVersion = v;
    }
}

void MapStore::setPath (const MapPath& p)
{
    edit = p;
    edit.count = juce::jlimit (0, maxPathPoints, edit.count);
    publish();
}

void MapStore::clear()
{
    edit.count = 0;
    publish();
}

juce::ValueTree MapStore::toValueTree() const
{
    MapPath copy;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        copy = shared;
    }
    juce::ValueTree t (IDs::mapPath);
    juce::StringArray pts;
    for (int i = 0; i < copy.count; ++i)
        pts.add (juce::String (copy.points[(size_t) i].x, 4) + ":" + juce::String (copy.points[(size_t) i].y, 4));
    t.setProperty (IDs::points, pts.joinIntoString (","), nullptr);
    return t;
}

void MapStore::fromValueTree (const juce::ValueTree& t)
{
    MapPath p;
    for (auto& token : juce::StringArray::fromTokens (t.getProperty (IDs::points).toString(), ",", ""))
    {
        if (p.count >= maxPathPoints) break;
        const auto xy = juce::StringArray::fromTokens (token, ":", "");
        if (xy.size() == 2)
            p.points[(size_t) p.count++] = { juce::jlimit (0.0f, 1.0f, xy[0].getFloatValue()),
                                             juce::jlimit (0.0f, 1.0f, xy[1].getFloatValue()) };
    }
    setPath (p);
}
}
