# Calage sur une vraie TB-303

## Enregistrements
Pack Freesound « TB303 Bass » de **pitmusic** (sons 8520 à 8549, 30 fichiers) : « The original
Roland TB-303 machine sampled through a Manley mic-pre ». Licence Creative Commons Sampling.
Notes tenues en C2 (~65,4 Hz), réglages de potards variés, dent de scie (01, 17-29) et carré
(02-16, 30), normalisées à -0,3 dBFS. Utilisés : les aperçus MP3 128 kb/s (les originaux
demandent un compte). Les fichiers ne sont pas dans ce dépôt.

## Méthode (Tools/fit303.py, Tools/fit303_global.py, Tools/Fit303.cpp)
- Mesure : niveau des harmoniques 1 à 100, trames de 40 ms, sur 2,6 s, normalisé sur le maximum,
  plancher -60 dB. Écart = moyenne quadratique des différences en dB.
- Pour chaque enregistrement, on cherche les potards (CUT OFF, RESONANCE, ENV MOD, DECAY) qui
  minimisent l'écart (évolution différentielle puis Nelder-Mead).
- Les constantes du modèle (Calibration dans Source/AcidVoice.h) sont cherchées sur 10
  enregistrements (01, 04, 06, 12, 13, 16, 18, 20, 21, 26), puis vérifiées sur les 20 autres.

## Résultat
| | avant (Open303) | après |
|---|---|---|
| 10 enregistrements du calage | 8,54 dB | 2,78 dB |
| 20 enregistrements jamais vus | 7,68 dB | 5,10 dB (meilleur sur 19/20) |

Constantes changées : passe-haut d'entrée 44,5 → 74 Hz ; passe-haut de la boucle de résonance
150 → 67 Hz ; décroissance du VCA, gate tenu 1,23 → 3,18 s ; part de l'enveloppe du filtre dans
le VCA 0,45 → 0,26 ; saturation de l'échelle presque linéaire (plage 3 → 10) ; carré : gain
70 → 98, décalage 4,37 → 4,54 ; résonance quasi inchangée.

## Limites
Pas d'accent, pas de slide, pas de note courte dans ces enregistrements : l'accent et le slide
restent ceux d'Open303 + nos ajouts. Positions réelles des potards inconnues (trouvées par
l'ajustement). Aperçus MP3 : au-delà de ~-50 dB, le bruit de codage compte.
