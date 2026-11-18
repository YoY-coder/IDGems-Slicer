# IDGems Slicer

Plugin instrument VST3 (MIDI → audio) de la famille IDGems : découpe de gros bounces de sound design
en slices, jouées et séquencées au MIDI avec beaucoup de random.

**État actuel : étapes 1 à 4 / 10**
- 4 emplacements de samples (glisser-déposer ou bouton CHARGER), chacun avec sa couleur.
- Découpe : transitoires (sensibilité), grille calée sur le tempo (1/4 à 1/64), parts égales,
  aléatoire (bouton RE-SLICE RANDOM), manuel (marqueurs à la souris). 16 à 256 slices.
- Anti-clic réglable, normalisation optionnelle par slice.
- Mode Clavier : une slice par touche à partir de la note racine (C1 par défaut).
- Aiguillage : à chaque note, tirage au sort du sample selon les 4 probabilités
  (normalisées, emplacements vides ignorés), option « pas de répétition », ou source forcée.
- Modes de jeu : **Clavier** (une slice par touche), **Scan** (une seule note, le SLICE SELECT choisit
  la slice ; modulable par random, vélocité, molette), **Accord** (plusieurs slices du même sample,
  décalées dans le temps). Pitch fixe ou qui suit la note, polyphonie réglable / mono.
- Séquenceur (onglet SÉQUENCEUR) : tourne tant qu'une note est tenue, calé sur le tempo d'Ableton,
  1/4 à 1/64 (et triolets), swing, groove. 11 lanes (Sample, Slice, Pitch, Reverse, Gate, Start,
  Vélocité, Ratchet, Proba, Freeze, Envoi FX), chacune avec sa longueur (polyrythmie),
  dessinable à la souris et randomisable.
- Tout est sauvegardé avec le projet Ableton (chemins des fichiers, slices exactes, pattern, réglages).

## Compilation automatique (sans rien installer)
Chaque envoi de code sur GitHub compile le plugin Windows et lance les tests automatiques.
Onglet **Actions** → dernière exécution « Build » → en bas, **Artifacts** :
- `IDGems-Slicer-Windows` : le plugin (`.vst3`) + la version autonome (`.exe`)
- `capture-interface` : une capture de l'interface générée par les tests

---

## Compiler sous Windows — pas à pas

### A. Installer Visual Studio (une seule fois, ~20 min)
1. Allez sur <https://visualstudio.microsoft.com/fr/downloads/> et téléchargez **Visual Studio Community** (gratuit).
2. Lancez l'installateur. Dans l'onglet **Charges de travail**, cochez **« Développement Desktop en C++ »**.
   (Laissez les options par défaut à droite : elles incluent CMake, c'est ce qu'il nous faut.)
3. Cliquez **Installer**, attendez la fin. Pas besoin d'ouvrir Visual Studio ensuite.

### B. Récupérer le code
1. Sur la page GitHub du projet, choisissez la branche `claude/new-session-egs55h`.
2. Bouton vert **Code → Download ZIP**.
3. Faites clic droit sur le zip → **Extraire tout…** vers un dossier simple, par ex. `C:\Dev\IDGems-Slicer`
   (évitez les chemins très longs ou OneDrive).

### C. Compiler
1. Dans le dossier extrait, **double-cliquez sur `build.bat`**.
2. Une fenêtre noire s'ouvre. La première fois, elle télécharge JUCE (connexion internet nécessaire)
   puis compile : comptez **5 à 15 minutes**. Les fois suivantes, c'est beaucoup plus rapide.
3. À la fin vous voyez `=== TERMINE ===`. Le plugin est dans le sous-dossier **`Plugin`** :
   - `Plugin\IDGems Slicer.vst3` → le plugin pour Ableton
   - `Plugin\IDGems Slicer.exe` → version autonome (sans DAW)

S'il y a `=== ECHEC ===`, copiez tout le texte de la fenêtre et envoyez-le-moi.

### D. Charger dans Ableton Live
1. Ableton → **Options → Préférences → onglet Plug-ins** (ou « Plug-ins / Fichiers » selon la version).
2. Activez **« Utiliser le dossier personnalisé VST3 »** (*Use VST3 Plug-In Custom Folder*).
3. **Parcourir** → sélectionnez le dossier `Plugin` (ex. `C:\Dev\IDGems-Slicer\Plugin`).
4. Cliquez **Rescan** (maintenez Alt en cliquant pour un rescan complet).
5. Dans le navigateur à gauche : **Plug-ins → VST3 → IDGems → IDGems Slicer**. Glissez-le sur une piste MIDI.

> Après chaque nouvelle compilation : **fermez Ableton avant** de lancer `build.bat`
> (Windows bloque le fichier tant qu'il est chargé), puis rouvrez Ableton.

---

## Structure du code
| Fichier | Rôle |
|---|---|
| `CMakeLists.txt` | Recette de construction (télécharge JUCE 8.0.15 automatiquement) |
| `build.bat` | Compilation en un double-clic |
| `Source/PluginProcessor.*` | Moteur audio + sauvegarde de l'état |
| `Source/PluginEditor.*` | Fenêtre redimensionnable (ratio fixe, taille mémorisée) |
| `Source/UI/MainView.*` | Interface principale (dessinée en 1280×800 puis mise à l'échelle) |
| `Source/UI/Theme.h` | Couleurs IDGems + couleurs des 4 samples |
| `Source/UI/IDGemsLookAndFeel.*` | Style des contrôles |
| `Source/UI/SlotPanel.*` | Carte d'un emplacement de sample |
| `Source/UI/SliceEditor.*`, `WaveformView.*` | Éditeur de découpe, forme d'onde |
| `Source/UI/PlayPanel.*` | Réglages de jeu et aiguillage |
| `Source/Engine/SampleManager.*` | Chargement (en arrière-plan) et découpe des 4 samples |
| `Source/Engine/Slicer.*` | Algorithmes de découpe |
| `Source/Engine/VoiceEngine.*` | Lecture des slices (voix pré-allouées, anti-clic) |
| `Source/Engine/Router.h` | Aiguillage par probabilité |
| `Source/Engine/Sequencer.*` | Lanes, grooves, transfert du pattern vers l'audio |
| `Source/UI/SequencerView.*` | Éditeur de lanes |
| `Tests/TestMain.cpp` | Tests automatiques du moteur |
| `Resources/Fonts/` | Votre police (.ttf / .otf) pour les titres, embarquée dans le plugin |
