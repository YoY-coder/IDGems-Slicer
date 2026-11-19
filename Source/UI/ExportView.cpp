#include "ExportView.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../Exporter.h"

namespace
{
    void setupLabel (juce::Label& l, const juce::String& t, juce::Component& parent, juce::Colour c = Theme::textDim)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (IDGemsLookAndFeel::monoFont (12.0f));
        l.setColour (juce::Label::textColourId, c);
        parent.addAndMakeVisible (l);
    }

    void drawBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        auto a = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (a);
        g.setColour (Theme::panelBorder);
        g.drawRect (a, 1.0f);
        g.setColour (c);
        g.fillRect (a.getX(), a.getY(), 14.0f, 2.0f);
        g.setFont (IDGemsLookAndFeel::monoFont (12.0f).boldened());
        g.drawText (title, r.reduced (8, 4).removeFromTop (16), juce::Justification::centredLeft);
    }

    const int recBarsChoices[] { 0, 1, 2, 4, 8, 16 };
    const int exportBarsChoices[] { 1, 2, 4, 8, 16, 32 };
}

//==============================================================================
void ExportView::FileDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool has = file.existsAsFile();
    g.setColour ((has ? Theme::blue : Theme::panelBorder).withAlpha (has ? 0.18f : 0.3f));
    g.fillRect (r);
    g.setColour (has ? Theme::blue : Theme::panelBorder);
    const float dashes[] { 4.0f, 3.0f };
    juce::Path p;
    p.addRectangle (r);
    juce::Path dashed;
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
    g.fillPath (dashed);

    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.setColour (has ? Theme::text : Theme::textDim);
    g.drawFittedText (has ? "GLISSER VERS ABLETON  >  " + file.getFileName() : juce::String ("aucun fichier"),
                      getLocalBounds().reduced (8, 2), juce::Justification::centred, 2);
}

void ExportView::FileDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (file.exists() && e.getDistanceFromDragStart() > 4)
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
ExportView::ExportView (IDGemsSlicerProcessor& p) : processor (p)
{
    // Resample
    recButton.setClickingTogglesState (false);
    recButton.setTooltip ("Enregistre la sortie du plugin. Avec une duree en mesures et Ableton en lecture, "
                          "l'enregistrement demarre a la prochaine mesure.");
    recButton.onClick = [this]
    {
        auto& rec = processor.getRecorder();
        const int st = rec.state.load();
        if (st == idg::Recorder::idle)
        {
            const int bars = recBarsChoices[juce::jmax (0, recLengthBox.getSelectedItemIndex())];
            rec.arm (bars, processor.getCurrentSampleRate(), processor.getHostBpm());
        }
        else if (st == idg::Recorder::armed) rec.cancel();
        else if (st == idg::Recorder::recording) rec.requestStop();
    };
    addAndMakeVisible (recButton);

    setupLabel (recLengthLabel, "DUREE", *this);
    recLengthBox.addItemList ({ "Jusqu'a l'arret", "1 mesure", "2 mesures", "4 mesures", "8 mesures", "16 mesures" }, 1);
    recLengthBox.setSelectedItemIndex (processor.getUiProperty ("recordBars", 3), juce::dontSendNotification);
    recLengthBox.onChange = [this] { processor.setUiProperty ("recordBars", recLengthBox.getSelectedItemIndex()); };
    addAndMakeVisible (recLengthBox);
    setupLabel (recStatus, "pret", *this, Theme::text);
    addAndMakeVisible (recHandle);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = loadButtons[(size_t) i];
        b.setButtonText ("> SAMPLE " + juce::String (i + 1));
        b.setColour (juce::TextButton::textColourOffId, Theme::sampleColour (i));
        b.setTooltip ("Recharge l'enregistrement dans l'emplacement " + juce::String (i + 1) + " (re-decoupe automatique)");
        b.onClick = [this, i] { loadRecordingInto (i); };
        addAndMakeVisible (b);
    }

    // Dossier
    setupLabel (folderLabel, "", *this, Theme::text);
    folderLabel.setMinimumHorizontalScale (0.6f);
    chooseFolderButton.setTooltip ("Dossier ou sont ecrits les enregistrements et les exports");
    chooseFolderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Dossier des enregistrements et exports", processor.getExportFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File()) processor.setExportFolder (fc.getResult());
                              });
    };
    openFolderButton.setTooltip ("Ouvre le dossier dans l'explorateur");
    openFolderButton.onClick = [this]
    {
        auto f = processor.getExportFolder();
        f.createDirectory();
        f.startAsProcess();
    };
    addAndMakeVisible (chooseFolderButton);
    addAndMakeVisible (openFolderButton);

    // Exports
    setupLabel (barsLabel, "MESURES", *this);
    barsBox.addItemList ({ "1", "2", "4", "8", "16", "32" }, 1);
    barsBox.setSelectedItemIndex (processor.getUiProperty ("exportBars", 2), juce::dontSendNotification);
    barsBox.onChange = [this] { processor.setUiProperty ("exportBars", barsBox.getSelectedItemIndex()); };
    barsBox.setTooltip ("Longueur du rendu du pattern");
    addAndMakeVisible (barsBox);

    wavButton.setTooltip ("Rend le pattern (sequenceur + tous les reglages actuels) en fichier WAV, hors temps reel");
    midiButton.setTooltip ("Ecrit les slices jouees par le pattern en fichier MIDI (note = racine + numero de slice : "
                           "a rejouer en Mode Clavier). Glissez-le ensuite dans Ableton.");
    slicesButton.setTooltip ("Exporte chaque slice des samples charges en fichier WAV separe (un dossier par sample)");
    wavButton.onClick = [this] { runExport (0); };
    midiButton.onClick = [this] { runExport (1); };
    slicesButton.onClick = [this] { runExport (2); };
    for (auto* b : { &wavButton, &midiButton, &slicesButton }) addAndMakeVisible (*b);
    addAndMakeVisible (exportHandle);
    setupLabel (exportStatus, "", *this, Theme::text);
}

ExportView::~ExportView() = default;

void ExportView::setStatus (const juce::String& s, bool error)
{
    exportStatus.setText (s, juce::dontSendNotification);
    exportStatus.setColour (juce::Label::textColourId, error ? juce::Colours::red : Theme::text);
}

void ExportView::runExport (int kind)
{
    if (busy) return;
    busy = true;
    setStatus ("export en cours...");
    juce::Component::SafePointer<ExportView> safe (this);
    auto done = [safe] (juce::Array<juce::File> files, juce::String msg)
    {
        if (safe == nullptr) return;
        safe->busy = false;
        safe->setStatus (msg + (files.isEmpty() ? juce::String() : "  >  " + files.getLast().getParentDirectory().getFullPathName()),
                         files.isEmpty());
        if (! files.isEmpty()) safe->exportHandle.setFile (files.getLast());
    };

    const int bars = exportBarsChoices[juce::jmax (0, barsBox.getSelectedItemIndex())];
    if (kind == 2) Exporter::exportSlices (processor, done);
    else           Exporter::renderPattern (processor, bars, kind == 0, kind == 1, done);
}

void ExportView::loadRecordingInto (int slot)
{
    if (! lastRecording.existsAsFile()) return;
    auto doLoad = [this, slot]
    {
        processor.getSampleManager().loadFile (slot, lastRecording);
        recStatus.setText ("charge dans le sample " + juce::String (slot + 1), juce::dontSendNotification);
    };

    if (processor.getSampleManager().isLoaded (slot))
    {
        juce::Component::SafePointer<ExportView> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Remplacer le sample " + juce::String (slot + 1) + " ?")
                                          .withMessage ("L'emplacement " + juce::String (slot + 1) + " contient deja \""
                                                        + processor.getSampleManager().getSlot (slot).file.getFileName()
                                                        + "\". Le remplacer par l'enregistrement ?")
                                          .withButton ("Remplacer")
                                          .withButton ("Annuler")
                                          .withAssociatedComponent (this),
                                      [safe, doLoad] (int result) { if (safe != nullptr && result == 1) doLoad(); });
    }
    else
    {
        doLoad();
    }
}

void ExportView::tick()
{
    auto& rec = processor.getRecorder();
    const int st = rec.state.load();
    const double sr = processor.getCurrentSampleRate();
    recBlink = std::fmod (recBlink + 0.06f, 1.0f);

    switch (st)
    {
        case idg::Recorder::armed:
            recButton.setButtonText ("ANNULER");
            recStatus.setText ("arme : demarre a la prochaine mesure (lancez la lecture)", juce::dontSendNotification);
            break;
        case idg::Recorder::recording:
            recButton.setButtonText ("STOP");
            recStatus.setText ("enregistrement  " + juce::String (rec.length.load() / sr, 1) + " s", juce::dontSendNotification);
            break;
        case idg::Recorder::finished:
        {
            juce::AudioBuffer<float> audio;
            if (rec.takeRecording (audio))
            {
                recStatus.setText ("ecriture du fichier...", juce::dontSendNotification);
                juce::Component::SafePointer<ExportView> safe (this);
                Exporter::saveRecording (std::move (audio), sr, processor.getExportFolder(),
                                         [safe] (juce::Array<juce::File> files, juce::String msg)
                                         {
                                             if (safe == nullptr) return;
                                             safe->recStatus.setText (msg, juce::dontSendNotification);
                                             if (! files.isEmpty())
                                             {
                                                 safe->lastRecording = files.getFirst();
                                                 safe->recHandle.setFile (safe->lastRecording);
                                             }
                                         });
            }
            break;
        }
        default:
            recButton.setButtonText ("REC");
            break;
    }

    for (auto& b : loadButtons) b.setEnabled (lastRecording.existsAsFile());
    for (auto* b : { &wavButton, &midiButton, &slicesButton }) b->setEnabled (! busy);
    folderLabel.setText (processor.getExportFolder().getFullPathName(), juce::dontSendNotification);
    repaint (recArea.withHeight (30));
}

void ExportView::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (26);
    recArea = r.removeFromTop (190);
    r.removeFromTop (10);
    folderArea = r.removeFromTop (60);
    r.removeFromTop (10);
    exportArea = r;

    {
        auto a = recArea.reduced (10, 6);
        a.removeFromTop (20);
        auto row = a.removeFromTop (34);
        recButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (14);
        recLengthLabel.setBounds (row.removeFromLeft (52));
        recLengthBox.setBounds (row.removeFromLeft (170).withSizeKeepingCentre (170, 24));
        row.removeFromLeft (14);
        recStatus.setBounds (row);
        a.removeFromTop (10);
        recHandle.setBounds (a.removeFromTop (44));
        a.removeFromTop (10);
        auto lr = a.removeFromTop (26);
        const int w = (lr.getWidth() - 3 * 8) / 4;
        for (auto& b : loadButtons) { b.setBounds (lr.removeFromLeft (w)); lr.removeFromLeft (8); }
    }
    {
        auto a = folderArea.reduced (10, 6);
        a.removeFromTop (18);
        auto row = a.removeFromTop (26);
        openFolderButton.setBounds (row.removeFromRight (90));
        row.removeFromRight (6);
        chooseFolderButton.setBounds (row.removeFromRight (110));
        row.removeFromRight (10);
        folderLabel.setBounds (row);
    }
    {
        auto a = exportArea.reduced (10, 6);
        a.removeFromTop (20);
        auto row = a.removeFromTop (28);
        barsLabel.setBounds (row.removeFromLeft (64));
        barsBox.setBounds (row.removeFromLeft (70).withSizeKeepingCentre (70, 24));
        row.removeFromLeft (14);
        const int w = (row.getWidth() - 2 * 8) / 3;
        wavButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (8);
        midiButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (8);
        slicesButton.setBounds (row);
        a.removeFromTop (10);
        exportHandle.setBounds (a.removeFromTop (44));
        a.removeFromTop (6);
        exportStatus.setBounds (a.removeFromTop (20));
    }
}

void ExportView::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    g.setColour (Theme::panel.withAlpha (0.85f));
    g.fillRect (a);
    g.setColour (Theme::panelBorder);
    g.drawRect (a, 1.0f);

    juce::Path corner;
    corner.startNewSubPath (a.getX(), a.getY() + 18.0f);
    corner.lineTo (a.getX(), a.getY());
    corner.lineTo (a.getX() + 18.0f, a.getY());
    g.setColour (Theme::orange);
    g.strokePath (corner, juce::PathStrokeType (2.0f));
    g.fillRect (a.getX(), a.getBottom() - 2.0f, 60.0f, 2.0f);

    auto r = getLocalBounds().reduced (12, 8);
    g.setFont (IDGemsLookAndFeel::titleFont (15.0f));
    g.setColour (Theme::orange);
    g.drawText ("RESAMPLE  //  EXPORT", r.removeFromTop (20), juce::Justification::centredLeft);

    drawBox (g, recArea, "RESAMPLE INTERNE", Theme::orange);
    drawBox (g, folderArea, "DOSSIER DES FICHIERS", Theme::blue);
    drawBox (g, exportArea, "EXPORT", Theme::orange);

    // Voyant d'enregistrement
    const int st = processor.getRecorder().state.load();
    if (st == idg::Recorder::recording || st == idg::Recorder::armed)
    {
        auto led = recButton.getBounds().toFloat().withWidth (10.0f).withHeight (10.0f).translated (-16.0f, 12.0f);
        const float alpha = st == idg::Recorder::recording ? 1.0f : (recBlink < 0.5f ? 1.0f : 0.2f);
        g.setColour (juce::Colours::red.withAlpha (alpha));
        g.fillEllipse (led.translated (recButton.getWidth() + 22.0f, 0.0f));
    }

    g.setFont (IDGemsLookAndFeel::monoFont (11.0f));
    g.setColour (Theme::textDim);
    g.drawFittedText ("Le MIDI exporte rejoue les memes slices en Mode Clavier (note = NOTE RACINE + numero de slice). "
                      "Les fichiers sont aussi ecrits dans le dossier ci-dessus.",
                      exportArea.reduced (10, 6).removeFromBottom (32), juce::Justification::centredLeft, 2);
}
