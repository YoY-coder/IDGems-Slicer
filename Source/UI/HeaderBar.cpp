#include "HeaderBar.h"
#include "Theme.h"
#include "IDGemsLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

HeaderBar::HeaderBar (IDGemsSlicerProcessor& p) : processor (p)
{
    prevButton.setTooltip ("Preset precedent (dossier des presets)");
    nextButton.setTooltip ("Preset suivant");
    nameButton.setTooltip ("Preset actuel : cliquez pour la liste des presets");
    saveButton.setTooltip ("Sauvegarde l'etat complet (samples, slices, 8 patterns, reglages) dans un preset");
    embedToggle.setTooltip ("Embarque les fichiers audio dans le preset (pour le partager ou changer d'ordinateur)");
    prevButton.onClick = [this] { if (auto f = PresetManager::neighbour (processor, -1); f.existsAsFile()) loadPreset (f); };
    nextButton.onClick = [this] { if (auto f = PresetManager::neighbour (processor, +1); f.existsAsFile()) loadPreset (f); };
    nameButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };
    embedToggle.setToggleState (processor.getUiProperty ("embedSamples", 0) != 0, juce::dontSendNotification);
    embedToggle.onClick = [this] { processor.setUiProperty ("embedSamples", embedToggle.getToggleState() ? 1 : 0); };
    for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &nextButton, &nameButton, &saveButton, &embedToggle })
        addAndMakeVisible (c);

    for (int i = 0; i < 8; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setTooltip ("Pattern " + juce::String (i + 1) + " de la banque. Au MIDI : note "
                      + juce::MidiMessage::getMidiNoteName (i, true, true, 3) + " ou Program Change " + juce::String (i + 1)
                      + ". Clic droit : copier / vider.");
        b.onClick = [this, i]
        {
            if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu()) return;
            processor.getPatternStore().selectPattern (i);
        };
        b.addMouseListener (this, false);
        addAndMakeVisible (b);
    }

    undoButton.setTooltip ("Annule la derniere modification (reglages, lanes, marqueurs, trajet, samples)");
    redoButton.setTooltip ("Retablit la modification annulee");
    undoButton.onClick = [this] { processor.getUndoHistory().undo(); };
    redoButton.onClick = [this] { processor.getUndoHistory().redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    processor.getUndoHistory().addChangeListener (this);
    tick();
}

HeaderBar::~HeaderBar()
{
    processor.getUndoHistory().removeChangeListener (this);
    for (auto& b : patternButtons) b.removeMouseListener (this);
}

void HeaderBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    undoButton.setEnabled (processor.getUndoHistory().canUndo());
    redoButton.setEnabled (processor.getUndoHistory().canRedo());
}

void HeaderBar::tick()
{
    const int cur = processor.getPatternStore().getCurrentIndex();
    if (cur != shownPattern)
    {
        shownPattern = cur;
        for (int i = 0; i < 8; ++i)
            patternButtons[(size_t) i].setToggleState (i == cur, juce::dontSendNotification);
    }
    const auto name = processor.getPresetName();
    if (nameButton.getButtonText() != name) nameButton.setButtonText (name);
    changeListenerCallback (nullptr);
}

void HeaderBar::mouseDown (const juce::MouseEvent& e)
{
    // Clic droit sur un bouton de pattern : copier / vider
    if (! e.mods.isPopupMenu()) return;
    for (int i = 0; i < 8; ++i)
        if (e.eventComponent == &patternButtons[(size_t) i])
            showPatternMenu (i);
}

void HeaderBar::showPatternMenu (int index)
{
    juce::PopupMenu m;
    const int cur = processor.getPatternStore().getCurrentIndex();
    m.addItem (1, "Copier le pattern " + juce::String (cur + 1) + " ici", index != cur);
    m.addItem (2, "Vider ce pattern");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternButtons[(size_t) index]),
                     [this, index, cur] (int r)
                     {
                         auto& ps = processor.getPatternStore();
                         if (r == 1) ps.copyPattern (cur, index);
                         if (r == 2)
                         {
                             auto bank = ps.getBank();
                             bank[(size_t) index] = idg::Pattern();
                             ps.setBank (bank);
                         }
                     });
}

void HeaderBar::showPresetMenu()
{
    juce::PopupMenu m;
    const auto files = PresetManager::list (processor);
    for (int i = 0; i < files.size(); ++i)
        m.addItem (i + 1, files[i].getFileNameWithoutExtension(), true,
                   files[i].getFileNameWithoutExtension() == processor.getPresetName());
    if (files.isEmpty()) m.addItem (-1, "(aucun preset : utilisez SAUVER)", false);
    m.addSeparator();
    m.addItem (1000, "Charger un fichier...");
    m.addItem (1001, "Ouvrir le dossier des presets");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton),
                     [this, files] (int r)
                     {
                         if (r >= 1 && r <= files.size()) loadPreset (files[r - 1]);
                         else if (r == 1000)
                         {
                             chooser = std::make_unique<juce::FileChooser> ("Charger un preset", PresetManager::folder (processor), "*.idgslicer");
                             chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                   [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadPreset (fc.getResult()); });
                         }
                         else if (r == 1001)
                         {
                             auto f = PresetManager::folder (processor);
                             f.createDirectory();
                             f.startAsProcess();
                         }
                     });
}

void HeaderBar::loadPreset (const juce::File& f)
{
    juce::String error;
    if (! PresetManager::load (processor, f, error))
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Preset").withMessage (error).withButton ("OK"), nullptr);
    else if (onPresetLoaded)
        onPresetLoaded();
}

void HeaderBar::savePreset()
{
    auto dir = PresetManager::folder (processor);
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Sauver le preset", dir.getChildFile (processor.getPresetName() + ".idgslicer"), "*.idgslicer");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("idgslicer");
                              juce::String error;
                              if (! PresetManager::save (processor, f, embedToggle.getToggleState(), error))
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                    .withTitle ("Preset").withMessage (error).withButton ("OK"), nullptr);
                          });
}

void HeaderBar::resized()
{
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop (r.getHeight() / 2).reduced (0, 2);
    auto row2 = r.reduced (0, 2);

    prevButton.setBounds (row1.removeFromLeft (24));
    row1.removeFromLeft (3);
    embedToggle.setBounds (row1.removeFromRight (104));
    row1.removeFromRight (4);
    saveButton.setBounds (row1.removeFromRight (74));
    row1.removeFromRight (6);
    nextButton.setBounds (row1.removeFromRight (24));
    row1.removeFromRight (3);
    nameButton.setBounds (row1);

    patternLabelArea = row2.removeFromLeft (70);
    redoButton.setBounds (row2.removeFromRight (56));
    row2.removeFromRight (4);
    undoButton.setBounds (row2.removeFromRight (56));
    row2.removeFromRight (10);
    const int w = (row2.getWidth() - 7 * 3) / 8;
    for (auto& b : patternButtons) { b.setBounds (row2.removeFromLeft (w)); row2.removeFromLeft (3); }
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.setFont (IDGemsLookAndFeel::monoFont (11.5f));
    g.setColour (Theme::textDim);
    g.drawText ("PATTERN", patternLabelArea, juce::Justification::centredLeft);
}
