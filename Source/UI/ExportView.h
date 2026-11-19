#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class IDGemsSlicerProcessor;

//==============================================================================
// Onglet EXPORT : resample interne (Record), dossier, exports WAV / MIDI /
// one-shots, glisser-deposer des fichiers vers Ableton.
//==============================================================================
class ExportView final : public juce::Component
{
public:
    explicit ExportView (IDGemsSlicerProcessor&);
    ~ExportView() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    // Poignee : glisser le fichier vers Ableton (ou l'explorateur)
    class FileDragHandle final : public juce::Component
    {
    public:
        void setFile (const juce::File& f) { file = f; repaint(); }
        const juce::File& getFile() const noexcept { return file; }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override {}
    private:
        juce::File file;
    };

private:
    void loadRecordingInto (int slot);
    void runExport (int kind);
    void setStatus (const juce::String&, bool error = false);

    IDGemsSlicerProcessor& processor;

    juce::TextButton recButton { "REC" };
    juce::ComboBox recLengthBox;
    juce::Label recLengthLabel, recStatus;
    FileDragHandle recHandle;
    std::array<juce::TextButton, 4> loadButtons;

    juce::Label folderLabel;
    juce::TextButton chooseFolderButton { "CHOISIR..." }, openFolderButton { "OUVRIR" };

    juce::ComboBox barsBox;
    juce::Label barsLabel;
    juce::TextButton wavButton { "PATTERN > WAV" }, midiButton { "PATTERN > MIDI" }, slicesButton { "SLICES > ONE-SHOTS" };
    FileDragHandle exportHandle;
    juce::Label exportStatus;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastRecording;
    bool busy = false;
    float recBlink = 0.0f;

    juce::Rectangle<int> recArea, folderArea, exportArea;
};
