#pragma once

/*
    Acid Machine — générateur de motifs aléatoires (bouton RANDOM).

    Chaque style fixe une gamme (en demi-tons depuis la tonique C) et des habitudes :
    part de toniques, d'octaves, de silences, d'accents, de slides, de TIE.
    Le pas 1 est toujours une tonique (sauf PSY GALLOP, qui laisse la place au kick).

    Sans dépendance (testable seul).
*/

#include "AcidSequencer.h"
#include <array>
#include <cstdint>
#include <vector>

namespace acid
{

class PatternGenerator
{
public:
    enum Style
    {
        minor = 0,     // mineur naturel
        major,         // majeur
        phrygian,      // mineur avec seconde mineure (darkpsy, hi-tech)
        dorian,        // mineur avec sixte majeure
        harmonicMinor, // mineur harmonique
        acid,          // acid classique : tonique, octave, quinte, tierce et septième mineures
        hiTech,        // hi-tech : doubles croches serrées, octaves, seconde mineure
        psyGallop,     // galop psytrance : silence sur chaque temps (le kick), trois notes entre
        chaos,         // chromatique, tout est permis
        numStyles
    };

    static const char* styleName (int style)
    {
        static const char* names[] = { "MINOR", "MAJOR", "PHRYGIAN", "DORIAN", "HARMONIC MINOR",
                                       "ACID", "HI-TECH", "PSY GALLOP", "CHAOS" };
        return style >= 0 && style < numStyles ? names[style] : "MINOR";
    }

    struct Habits
    {
        std::vector<int> scale;     // demi-tons autorisés (0..12)
        double root;                // part de toniques
        double octaveUp, octaveDown;
        double rest, tie;
        double accent, slide;
    };

    static Habits habitsFor (int style)
    {
        switch (style)
        {
            case major:         return { { 0, 2, 4, 5, 7, 9, 11, 12 },  0.35, 0.15, 0.08, 0.12, 0.05, 0.22, 0.18 };
            case phrygian:      return { { 0, 1, 3, 5, 7, 8, 10, 12 },  0.45, 0.20, 0.08, 0.10, 0.04, 0.28, 0.16 };
            case dorian:        return { { 0, 2, 3, 5, 7, 9, 10, 12 },  0.38, 0.15, 0.10, 0.14, 0.05, 0.24, 0.18 };
            case harmonicMinor: return { { 0, 2, 3, 5, 7, 8, 11, 12 },  0.38, 0.15, 0.08, 0.12, 0.05, 0.25, 0.18 };
            case acid:          return { { 0, 3, 7, 10, 12 },           0.50, 0.25, 0.10, 0.10, 0.04, 0.30, 0.22 };
            case hiTech:        return { { 0, 1, 3, 7, 12 },            0.55, 0.30, 0.05, 0.04, 0.00, 0.35, 0.08 };
            case psyGallop:     return { { 0, 1, 3, 7, 12 },            0.65, 0.15, 0.05, 0.00, 0.00, 0.30, 0.05 };
            case chaos:         return { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }, 0.20, 0.25, 0.20, 0.15, 0.08, 0.35, 0.25 };
            case minor:
            default:            return { { 0, 2, 3, 5, 7, 8, 10, 12 },  0.40, 0.15, 0.08, 0.12, 0.05, 0.25, 0.18 };
        }
    }

    /** Nouveau motif de 16 pas. */
    static Pattern generate (int style, uint32_t seed)
    {
        Rng rng { seed * 2654435761u + 12345u };
        const auto h = habitsFor (style);
        Pattern p;
        p.length = 16;

        for (int i = 0; i < 16; ++i)
        {
            Step s;
            const bool onBeat = i % 4 == 0;

            if (style == psyGallop && onBeat)
            {
                s.time = Step::rest;
                p.steps[(size_t) i] = s;
                continue;
            }

            // Silence / TIE (jamais sur le pas 1)
            if (i > 0)
            {
                const double r = rng.uniform();
                if (r < h.rest * (onBeat ? 0.5 : 1.0))       { s.time = Step::rest; p.steps[(size_t) i] = s; continue; }
                if (r < h.rest + h.tie && p.steps[(size_t) i - 1].time != Step::rest) { s.time = Step::tie; p.steps[(size_t) i] = s; continue; }
            }

            // Note
            const bool firstNote = i == 0 || (style == psyGallop && i == 1);
            if (firstNote || rng.uniform() < h.root)
                s.pitch = 0;
            else
                s.pitch = h.scale[(size_t) rng.below ((uint32_t) h.scale.size())];

            const double o = rng.uniform();
            if (! firstNote && o < h.octaveUp)                    s.octave = 2;
            else if (! firstNote && o < h.octaveUp + h.octaveDown) s.octave = 0;

            // Accents plutôt à contretemps, slides plutôt avant une note
            s.accent = rng.uniform() < h.accent * (onBeat ? 0.6 : 1.2);
            s.slide = rng.uniform() < h.slide;

            p.steps[(size_t) i] = s;
        }

        // Un slide n'a de sens que vers une note : sinon on l'enlève
        for (int i = 0; i < 16; ++i)
        {
            auto& s = p.steps[(size_t) i];
            const auto& next = p.steps[(size_t) ((i + 1) % 16)];
            if (s.slide && (s.time == Step::rest || next.time != Step::note))
                s.slide = false;
            if (s.time != Step::note)
                s.accent = false;
        }

        // Au moins deux accents et pas de motif trop vide
        int accents = 0, notes = 0;
        for (const auto& s : p.steps) { accents += s.accent; notes += s.time == Step::note; }
        for (int i = 0; accents < 2 && i < 64; ++i)
        {
            auto& s = p.steps[(size_t) rng.below (16)];
            if (s.time == Step::note && ! s.accent) { s.accent = true; ++accents; }
        }
        for (int i = 1; notes < 8 && i < 16; ++i)
        {
            auto& s = p.steps[(size_t) i];
            if (s.time == Step::rest && ! (style == psyGallop && i % 4 == 0)) { s = Step {}; ++notes; }
        }

        return p;
    }

private:
    struct Rng
    {
        uint32_t state;
        uint32_t next()  { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
        double uniform() { return (next() >> 8) * (1.0 / 16777216.0); }
        uint32_t below (uint32_t n) { return (uint32_t) (uniform() * n) % n; }
    };
};

} // namespace acid
