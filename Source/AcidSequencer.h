#pragma once

/*
    Acid Machine — séquenceur 16 pas, façon TB-303, calé sur la position de l'hôte.

    Un pas : une note (C à C' de l'octave), une octave (DOWN / MID / UP), un mode de
    temps (NOTE / TIE / REST), ACCENT et SLIDE.
      - NOTE : la note est jouée ; le gate tombe à la moitié du pas.
      - SLIDE sur un pas : le gate reste ouvert jusqu'au pas suivant, qui glisse vers sa
        note sans redéclencher les enveloppes (si c'est une NOTE).
      - TIE : prolonge la note précédente (gate tenu, pas de nouvelle attaque).
      - REST : silence.
    La position dans le motif vient de la position de l'hôte (en noires) : le pas 1
    tombe sur le temps 1 de chaque mesure quand le motif fait 16 pas en 1/16.

    Sans dépendance (testable seul).
*/

#include <array>
#include <cmath>
#include <functional>

namespace acid
{

struct Step
{
    enum Time { note = 0, tie = 1, rest = 2 };

    int pitch = 0;      // 0..12 (C..C')
    int octave = 1;     // 0 = DOWN, 1 = MID, 2 = UP
    int time = note;
    bool accent = false;
    bool slide = false;
};

struct Pattern
{
    static constexpr int maxSteps = 16;
    std::array<Step, maxSteps> steps {};
    int length = 16;
};

class AcidSequencer
{
public:
    /** Note MIDI du C de l'octave MID (C1 dans Live = 65,4 Hz). */
    static constexpr int baseNote = 36;
    static constexpr double gateFraction = 0.5;

    enum class EventType { trigger, slide, release };

    struct Event
    {
        EventType type;
        int note = 0;
        bool accent = false;
    };

    using Callback = std::function<void (int sampleOffset, const Event&)>;

    void setPattern (const Pattern& p) noexcept     { pattern = p; if (pattern.length < 1) pattern.length = 1; if (pattern.length > Pattern::maxSteps) pattern.length = Pattern::maxSteps; }
    const Pattern& getPattern() const noexcept      { return pattern; }

    /** Durée d'un pas en noires (0,25 = double croche). */
    void setStepBeats (double beats) noexcept       { stepBeats = beats > 1.0e-3 ? beats : 0.25; }
    /** 0..1 : retard des pas pairs (2, 4…), 1 = un tiers de pas (shuffle en triolet). */
    void setSwing (double amount) noexcept          { swing = amount < 0.0 ? 0.0 : (amount > 1.0 ? 1.0 : amount); }
    /** Transposition en demi-tons, prise à chaque nouvelle note. */
    void setTranspose (int semitones) noexcept      { transpose = semitones; }

    /** Pas en cours (0..15) ou -1 à l'arrêt. */
    int getCurrentStep() const noexcept             { return currentStep; }

    void stop (const Callback& emit, int sampleOffset = 0)
    {
        if (gateHeld)
            emit (sampleOffset, { EventType::release, lastNote, false });
        gateHeld = false;
        running = false;
        currentStep = -1;
        pendingRelease = -1.0;
    }

    /**
        Un bloc : position de l'hôte au début (en noires), tempo, nombre d'échantillons.
        Les événements sont envoyés dans l'ordre, avec leur position dans le bloc.
    */
    void process (bool playing, double ppqStart, double bpm, double sampleRate, int numSamples, const Callback& emit)
    {
        if (! playing || bpm <= 0.0 || sampleRate <= 0.0)
        {
            if (running || gateHeld)
                stop (emit);
            return;
        }

        const double beatsPerSample = bpm / (60.0 * sampleRate);
        const double ppqEnd = ppqStart + beatsPerSample * numSamples;

        // Saut de la position (boucle de l'hôte, déplacement du curseur) : on coupe et on reprend au prochain pas
        if (running && std::abs (ppqStart - expectedPpq) > 0.5 * beatsPerSample + 1.0e-9)
        {
            if (gateHeld)
                emit (0, { EventType::release, lastNote, false });
            gateHeld = false;
            pendingRelease = -1.0;
            running = false;
        }

        if (! running)
        {
            // Premier pas : celui qui commence à partir d'ici (on ne rattrape pas un pas déjà commencé)
            nextIndex = (long long) std::floor (ppqStart / stepBeats) - 1;
            while (stepStart (nextIndex) < ppqStart - 1.0e-6 * beatsPerSample)
                ++nextIndex;
            running = true;
        }

        expectedPpq = ppqEnd;

        // Un événement tombe sur le premier échantillon à partir de sa position (à un millionième
        // d'échantillon près, pour que les arrondis de la position de l'hôte ne le décalent pas)
        auto offsetOf = [&] (double ppq)
        {
            const double o = std::ceil ((ppq - ppqStart) / beatsPerSample - 1.0e-6);
            return o < 0.0 ? 0LL : (long long) o;
        };

        for (;;)
        {
            const double startPpq = stepStart (nextIndex);
            const bool releaseFirst = pendingRelease >= 0.0 && pendingRelease <= startPpq;

            if (releaseFirst)
            {
                const auto offset = offsetOf (pendingRelease);
                if (offset >= numSamples)
                    break;
                emit ((int) offset, { EventType::release, lastNote, false });
                gateHeld = false;
                pendingRelease = -1.0;
                continue;
            }

            const auto offset = offsetOf (startPpq);
            if (offset >= numSamples)
                break;

            startStep (nextIndex, (int) offset, emit);
            ++nextIndex;
        }
    }

private:
    double stepStart (long long index) const noexcept
    {
        const double base = (double) index * stepBeats;
        return (index & 1) ? base + swing * stepBeats / 3.0 : base;
    }

    int patternIndex (long long index) const noexcept
    {
        const long long n = pattern.length;
        return (int) (((index % n) + n) % n);
    }

    int noteOf (const Step& s) const noexcept
    {
        int n = baseNote + 12 * (s.octave - 1) + s.pitch + transpose;
        return n < 0 ? 0 : (n > 127 ? 127 : n);
    }

    void startStep (long long index, int offset, const Callback& emit)
    {
        const int i = patternIndex (index);
        const Step& s = pattern.steps[(size_t) i];
        const Step& next = pattern.steps[(size_t) patternIndex (index + 1)];
        currentStep = i;

        bool sounding = false;

        if (s.time == Step::note)
        {
            const int n = noteOf (s);
            if (gateHeld && previousSlides)
                emit (offset, { EventType::slide, n, s.accent });
            else
                emit (offset, { EventType::trigger, n, s.accent });
            lastNote = n;
            gateHeld = true;
            sounding = true;
        }
        else if (s.time == Step::tie && gateHeld)
        {
            sounding = true;   // la note précédente continue
        }
        else if (gateHeld)
        {
            emit (offset, { EventType::release, lastNote, false });
            gateHeld = false;
        }

        if (sounding)
        {
            const bool holds = (s.slide && next.time == Step::note) || next.time == Step::tie;
            previousSlides = s.slide && next.time == Step::note;
            pendingRelease = holds ? -1.0 : stepStart (index) + gateFraction * stepBeats;
        }
        else
        {
            previousSlides = false;
            pendingRelease = -1.0;
        }
    }

    Pattern pattern;
    double stepBeats = 0.25, swing = 0.0;
    int transpose = 0;

    bool running = false, gateHeld = false, previousSlides = false;
    long long nextIndex = 0;
    double expectedPpq = 0.0, pendingRelease = -1.0;
    int lastNote = 0, currentStep = -1;
};

} // namespace acid
