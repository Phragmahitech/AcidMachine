# Acid Machine (by Phragma)

Instrument VST3 qui émule une basse acid à la TB-303 : un oscillateur (dent de scie ou carré
façonné), filtre à échelle de diodes, enveloppe de filtre, accent avec son balayage qui
s'accumule, slide, et un séquenceur de 16 pas calé sur le DAW.

- Moteur : `Source/AcidVoice.h` (sans dépendance), basé sur Open303 (Robin Schmidt, MIT) et
  ses valeurs mesurées sur de vraies machines ; ajouts : saturation dans la boucle du filtre,
  balayage d'accent lié à la résonance, écrêtage doux du VCA, suréchantillonnage x4.
- Séquenceur : `Source/AcidSequencer.h` ; banque d'usine : `Source/FactoryPresets.h`.
- JUCE 8.0.15, C++17, CMake. Tests : `-DACIDMACHINE_BUILD_TESTS=ON`.

Aucun lien avec Roland : « TB-303 » désigne seulement l'instrument de référence.
