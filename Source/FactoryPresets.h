#pragma once

/*
    Acid Machine — banque d'usine.

    Chaque preset donne les potards (en %) et un motif de 16 pas, écrit en C (la
    transposition TRANSPOSE ou une note MIDI le met dans la bonne tonalité).

    Écriture d'un pas :
      C C# D D# E F F# G G# A A# B C'   la note (C' = C de l'octave du dessus)
      ^ / v                             octave UP / DOWN
      !                                 accent
      ~                                 slide vers le pas suivant
      -                                 silence (REST)
      =                                 prolonge la note précédente (TIE)
*/

#include "AcidSequencer.h"
#include <array>
#include <cstring>
#include <string>

namespace acid
{

struct FactoryPreset
{
    const char* name;
    float cutoff, resonance, envMod, decay, accent;   // %
    bool square;
    int rate;          // 0 = 1/8, 1 = 1/16, 2 = 1/16T, 3 = 1/32
    float swing;       // %
    int length;
    std::array<const char*, 16> steps;
};

/** Lit un pas écrit comme ci-dessus. */
inline Step parseStep (const char* text)
{
    Step s;
    std::string t (text);

    if (t == "-") { s.time = Step::rest; return s; }
    if (t == "=") { s.time = Step::tie; return s; }

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    size_t used = 0;
    for (int n = 11; n >= 0; --n)
    {
        const auto len = std::strlen (names[n]);
        if (t.compare (0, len, names[n]) == 0 && (len == 2 || t.size() == 1 || t[1] != '#'))
        {
            s.pitch = n;
            used = len;
            break;
        }
    }

    for (size_t i = used; i < t.size(); ++i)
    {
        switch (t[i])
        {
            case '\'': s.pitch = 12; break;
            case '^':  s.octave = 2; break;
            case 'v':  s.octave = 0; break;
            case '!':  s.accent = true; break;
            case '~':  s.slide = true; break;
            default: break;
        }
    }

    return s;
}

inline Pattern parsePattern (const FactoryPreset& p)
{
    Pattern pattern;
    pattern.length = p.length;
    for (size_t i = 0; i < 16; ++i)
        pattern.steps[i] = parseStep (p.steps[i]);
    return pattern;
}

/** La banque. Le premier (INIT) est l'état par défaut du plugin. */
inline const std::array<FactoryPreset, 18>& factoryPresets()
{
    static const std::array<FactoryPreset, 18> list {{
        { "INIT",                     50, 50, 50, 50, 50, false, 1,  0, 16,
          {{ "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C", "C" }} },

        // ---- Mineur -----------------------------------------------------------
        { "MINOR - Rolling Squelch",  34, 76, 62, 38, 72, false, 1,  0, 16,
          {{ "C!", "C", "C^", "-", "D#", "C~", "G", "C", "C!", "A#v", "C", "C^", "-", "G", "D#~", "F" }} },

        { "MINOR - Phrygian Hunter",  30, 82, 70, 28, 80, false, 1,  0, 16,
          {{ "C!", "C", "C#", "C", "C", "C^~", "C", "D#", "C!", "C", "C#~", "C", "G", "C", "C#^", "C" }} },

        { "MINOR - Harmonic Night",   40, 72, 55, 55, 62, false, 1,  0, 16,
          {{ "C!", "-", "G", "G#", "B~", "C'", "G", "-", "D#!", "D", "C~", "Bv", "C", "G", "G#!", "G" }} },

        { "MINOR - Dorian Groove",    44, 62, 50, 50, 55, false, 1, 22, 16,
          {{ "C!", "C", "A#v~", "C", "D#", "F", "C", "A", "G~", "F", "D#!", "C", "-", "C^", "A#", "G" }} },

        { "MINOR - Deep Sub",         14, 52, 40, 60, 50, true,  1,  0, 16,
          {{ "Cv!", "Cv", "C", "Cv", "Cv", "G#v", "Cv", "C~", "Cv!", "Cv", "D#", "Cv", "Cv", "A#v~", "C", "Gv" }} },

        // ---- Majeur -----------------------------------------------------------
        { "MAJOR - Sunrise",          50, 60, 46, 55, 52, false, 1,  0, 16,
          {{ "C!", "E", "G", "C^", "B", "G~", "A", "G", "F!", "E", "D~", "E", "G", "-", "C^~", "B" }} },

        { "MAJOR - Mixolydian Bounce",40, 66, 56, 45, 62, true,  1, 15, 16,
          {{ "C!", "C", "A#v", "C^", "G", "C", "E~", "F", "C!", "C", "A#", "G~", "A", "C", "E", "D" }} },

        { "MAJOR - Happy Squelch",    55, 56, 50, 45, 56, false, 1,  0, 16,
          {{ "C!", "-", "D", "E", "G!", "E", "D~", "C", "Av", "C", "E", "G~", "A", "G", "E!", "D" }} },

        { "MAJOR - Lydian Float",     50, 70, 40, 70, 42, false, 1,  0, 16,
          {{ "C!", "=", "G", "F#", "E~", "D", "E", "B", "C'!", "=", "A", "F#~", "G", "E", "D", "Bv" }} },

        // ---- Acid -------------------------------------------------------------
        { "ACID - Classic 16ths",     35, 86, 66, 35, 86, false, 1,  0, 16,
          {{ "C!", "C", "C^", "C", "C", "C^~", "D#^", "C", "C!", "C", "A#", "C^", "C", "G", "C^~", "C" }} },

        { "ACID - Octave Jumper",     30, 76, 60, 30, 76, false, 1,  0, 16,
          {{ "Cv!", "C", "Cv", "C^", "Cv", "C", "Cv~", "C^", "Cv!", "C", "D#", "Cv", "C^!", "Cv", "G", "C" }} },

        { "ACID - Screamer",          68, 95, 80, 25, 100, false, 1, 0, 16,
          {{ "C!", "C^", "C", "D#^!", "C", "C^~", "G", "C", "C!", "F^", "C", "C^", "D#!~", "C^", "A#", "G" }} },

        { "ACID - Slide City",        30, 80, 60, 60, 70, false, 1,  0, 16,
          {{ "C~", "D#~", "F~", "G!", "G~", "F~", "D#", "C!", "C~", "A#v~", "C~", "G!", "D#~", "C~", "F", "C" }} },

        { "ACID - Wow Accents",       25, 95, 50, 30, 100, false, 1, 0, 16,
          {{ "C!", "C!", "C", "C!", "C^!", "C", "C!", "D#", "C!", "C!", "C", "G!", "C!", "C", "A#!", "C" }} },

        { "ACID - Square Bubbles",    30, 72, 60, 35, 70, true,  1,  0, 16,
          {{ "C!", "G", "C^", "G", "D#!", "C", "G~", "C^", "C!", "G", "A#", "G", "F!~", "D#", "D", "C" }} },

        { "ACID - Psy Gallop",        24, 72, 66, 20, 80, false, 1,  0, 16,
          {{ "-", "C", "C^", "C!", "-", "C", "C", "D#!", "-", "C", "C^", "C!", "-", "C#", "C", "G!" }} },

        { "ACID - Hi-Tech Run",       40, 86, 70, 15, 90, false, 1,  0, 16,
          {{ "C!", "C^", "C", "C^", "C#!", "C^", "C", "G", "C!", "C^", "D#", "C^", "C!", "F#^", "C", "G^" }} },
    }};

    return list;
}

} // namespace acid
