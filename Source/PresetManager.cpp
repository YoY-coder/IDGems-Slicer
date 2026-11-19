#include "PresetManager.h"
#include "PluginProcessor.h"

namespace PresetManager
{
namespace
{
    const juce::Identifier embedded     { "embeddedAudio" };
    const juce::Identifier embeddedName { "embeddedName" };
    const juce::Identifier pathId       { "path" };
    constexpr int magic = 0x49444753;   // "IDGS"
}

juce::File folder (const IDGemsSlicerProcessor& p)
{
    return p.getExportFolder().getChildFile ("Presets");
}

juce::Array<juce::File> list (const IDGemsSlicerProcessor& p)
{
    auto files = folder (p).findChildFiles (juce::File::findFiles, false, "*.idgslicer");
    files.sort();
    return files;
}

bool save (IDGemsSlicerProcessor& p, const juce::File& file, bool embedSamples, juce::String& error)
{
    p.setPresetName (file.getFileNameWithoutExtension());
    auto tree = p.createStateTree();

    // Reglages de fenetre : propres a la machine, pas au preset
    for (auto id : { "editorWidth", "editorHeight", "exportFolder" })
        tree.removeProperty (id, nullptr);

    if (embedSamples)
    {
        auto slots = tree.getChildWithName ("Slots");
        for (auto slot : slots)
        {
            const juce::File f (slot.getProperty (pathId).toString());
            juce::MemoryBlock mb;
            if (f.existsAsFile() && f.loadFileAsData (mb))
            {
                slot.setProperty (embedded, mb, nullptr);
                slot.setProperty (embeddedName, f.getFileName(), nullptr);
            }
        }
    }

    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream fos (temp.getFile());
        if (! fos.openedOk()) { error = "Impossible d'ecrire " + file.getFullPathName(); return false; }
        fos.writeInt (magic);
        juce::GZIPCompressorOutputStream gz (fos);
        tree.writeToStream (gz);
    }
    if (! temp.overwriteTargetFileWithTemporary()) { error = "Impossible d'ecrire " + file.getFullPathName(); return false; }
    return true;
}

bool load (IDGemsSlicerProcessor& p, const juce::File& file, juce::String& error)
{
    juce::FileInputStream fis (file);
    if (! fis.openedOk() || fis.readInt() != magic) { error = "Fichier de preset invalide"; return false; }
    juce::GZIPDecompressorInputStream gz (fis);
    auto tree = juce::ValueTree::readFromStream (gz);
    if (! tree.isValid()) { error = "Preset illisible"; return false; }

    // Samples embarques : extraits a cote des presets puis charges depuis la
    auto slots = tree.getChildWithName ("Slots");
    for (auto slot : slots)
    {
        if (! slot.hasProperty (embedded)) continue;
        if (auto* mb = slot.getProperty (embedded).getBinaryData())
        {
            auto dir = folder (p).getChildFile ("Samples").getChildFile (file.getFileNameWithoutExtension());
            dir.createDirectory();
            auto target = dir.getChildFile (slot.getProperty (embeddedName).toString());
            if (! target.existsAsFile() || target.getSize() != (juce::int64) mb->getSize())
                target.replaceWithData (mb->getData(), mb->getSize());
            slot.setProperty (pathId, target.getFullPathName(), nullptr);
        }
        slot.removeProperty (embedded, nullptr);
        slot.removeProperty (embeddedName, nullptr);
    }

    p.applyStateTree (tree, true);
    p.setPresetName (file.getFileNameWithoutExtension());
    return true;
}

juce::File neighbour (const IDGemsSlicerProcessor& p, int direction)
{
    const auto files = list (p);
    if (files.isEmpty()) return {};
    int idx = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == p.getPresetName()) idx = i;
    const int n = files.size();
    const int next = idx < 0 ? (direction > 0 ? 0 : n - 1) : ((idx + direction) % n + n) % n;
    return files[next];
}
}
