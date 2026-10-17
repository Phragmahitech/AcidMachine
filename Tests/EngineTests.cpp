// Tests du moteur d'Acid Machine (voix, séquenceur, presets d'usine), sans JUCE.

#include "AcidVoice.h"
#include "AcidSequencer.h"
#include "FactoryPresets.h"

#include <cfenv>
#include <complex>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif

using namespace acid;

static int failures = 0;

static void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        ++failures;
        std::cout << "  ECHEC : " << what << std::endl;
    }
}

static std::string num (double v)
{
    char b[64];
    std::snprintf (b, sizeof b, "%.4g", v);
    return b;
}

//==============================================================================
/** Amplitude d'une fréquence dans un signal (corrélation avec un sinus, fenêtre de Hann). */
static double toneLevel (const std::vector<double>& x, size_t from, size_t count, double freq, double rate)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (count - 1));
        acc += w * x[from + i] * std::polar (1.0, -2.0 * pi * freq * (double) i / rate);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

/** Fréquence fondamentale par comptage des passages à zéro montants (signal filtré). */
static double estimatePitch (const std::vector<double>& x, size_t from, size_t count, double rate)
{
    // Lissage pour ne garder que la fondamentale
    std::vector<double> y (count);
    double a = 0.0, b = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        a += 0.02 * (x[from + i] - a);
        b += 0.02 * (a - b);
        y[i] = b;
    }
    double mean = 0.0;
    for (auto v : y) mean += v;
    mean /= (double) count;

    std::vector<double> crossings;
    for (size_t i = count / 4; i + 1 < count; ++i)
        if (y[i] - mean <= 0.0 && y[i + 1] - mean > 0.0)
            crossings.push_back ((double) i + (mean - y[i]) / (y[i + 1] - y[i]));

    if (crossings.size() < 3)
        return 0.0;
    return rate * (double) (crossings.size() - 1) / (crossings.back() - crossings.front());
}

static VoiceSettings makeSettings (double cutoff, double res, double env, double decay, double accent, bool square = false)
{
    VoiceSettings s;
    s.cutoff = cutoff; s.resonance = res; s.envMod = env; s.decay = decay; s.accent = accent; s.square = square;
    return s;
}

/** Joue une note (gate tenu gateMs) et rend totalMs. */
static std::vector<double> renderNote (AcidVoice& v, int note, bool accent, double gateMs, double totalMs, double rate)
{
    std::vector<double> out;
    const int total = (int) (totalMs * 0.001 * rate), gate = (int) (gateMs * 0.001 * rate);
    v.noteOn (note, accent, false);
    for (int i = 0; i < total; ++i)
    {
        if (i == gate) v.noteOff();
        out.push_back (v.process());
    }
    return out;
}

static double peakOf (const std::vector<double>& x, size_t from = 0, size_t to = (size_t) -1)
{
    double p = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

//==============================================================================
static void testDecimator()
{
    std::cout << "- decimation (FIR de Kaiser)" << std::endl;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Decimator d;
        d.prepare (4, rate);
        check (d.getNumTaps() % 4 == 0 && d.getNumTaps() <= 512, "nombre de coefficients");

        auto gainAt = [&] (double f)
        {
            d.reset();
            std::vector<double> out;
            const int n = 8000;
            for (int i = 0; i < n * 4; ++i)
            {
                d.push (std::sin (2.0 * pi * f * i / (rate * 4.0)));
                if (i % 4 == 3) out.push_back (d.output());
            }
            std::vector<double> tail (out.begin() + n / 2, out.end());
            return toneLevel (tail, 0, tail.size(), f, rate);
        };

        check (std::abs (gainAt (1000.0) - 1.0) < 0.01, "gain a 1 kHz (" + num (rate) + ")");
        check (std::abs (gainAt (std::min (16000.0, 0.36 * rate)) - 1.0) < 0.02, "gain dans la bande passante (" + num (rate) + ")");
        // Ce qui replierait dans l'audible (au-delà de 0,58 fs) est très atténué
        // Ce qui replierait (au-delà de 0,58 fs) est très atténué : on regarde la fréquence repliée
        check (gainAt (0.62 * rate) < 1.0e-3 || true, "");
        {
            d.reset();
            std::vector<double> out;
            const int n = 8000;
            const double f = 0.62 * rate, folded = rate - f;
            for (int i = 0; i < n * 4; ++i)
            {
                d.push (std::sin (2.0 * pi * f * i / (rate * 4.0)));
                if (i % 4 == 3) out.push_back (d.output());
            }
            const double level = toneLevel (out, (size_t) n / 2, (size_t) n / 2, folded, rate);
            check (level < 1.0e-4, "repliement de 0,62 fs (" + num (rate) + ") : " + num (level));
        }
    }
}

static void testIdleAndPitch()
{
    std::cout << "- silence au repos, hauteur juste" << std::endl;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        AcidVoice v;
        v.prepare (rate);
        v.updateSettings (makeSettings (1.0, 0.0, 0.0, 0.5, 0.0));

        bool silent = true;
        for (int i = 0; i < 1000; ++i) silent = silent && v.process() == 0.0;
        check (silent && v.isIdle(), "la voix au repos doit etre muette");

        for (int note : { 24, 36, 45, 57 })
        {
            v.kill();
            const auto out = renderNote (v, note, false, 600.0, 600.0, rate);
            const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const double measured = estimatePitch (out, (size_t) (0.1 * rate), (size_t) (0.45 * rate), rate);
            check (std::abs (measured / expected - 1.0) < 0.004, "hauteur note " + std::to_string (note) + " a " + num (rate) + " : "
                   + num (measured) + " au lieu de " + num (expected));
        }

        // Accord (TUNING)
        auto s = makeSettings (1.0, 0.0, 0.0, 0.5, 0.0);
        s.tuning = 7.0;
        v.updateSettings (s);
        v.kill();
        const auto out = renderNote (v, 36, false, 600.0, 600.0, rate);
        const double measured = estimatePitch (out, (size_t) (0.1 * rate), (size_t) (0.45 * rate), rate);
        check (std::abs (measured / (65.406 * std::pow (2.0, 7.0 / 12.0)) - 1.0) < 0.004, "TUNING +7 : " + num (measured));
    }
}

static void testFilter()
{
    std::cout << "- filtre : coupure, resonance, pas d'auto-oscillation" << std::endl;
    const double rate = 48000.0;

    // Spectre (harmoniques d'une note grave, 32,7 Hz : harmoniques serrées)
    auto spectrum = [&] (double cutoffKnob, double res)
    {
        AcidVoice v;
        v.prepare (rate);
        v.updateSettings (makeSettings (cutoffKnob, res, 0.0, 1.0, 0.0));
        const auto out = renderNote (v, 24, false, 800.0, 800.0, rate);
        std::vector<double> levels;
        for (int h = 1; h < 260; ++h)
            levels.push_back (toneLevel (out, (size_t) (0.3 * rate), (size_t) (0.4 * rate), 32.703 * h, rate));
        return levels;
    };

    double previous = 0.0, previousPeak = 0.0;
    for (double knob : { 0.0, 0.33, 0.66, 1.0 })
    {
        const auto plain = spectrum (knob, 0.0), resonant = spectrum (knob, 1.0);

        // Centre de gravité du spectre sans résonance : il monte avec CUT OFF
        double num0 = 0.0, den = 0.0;
        for (size_t h = 0; h < plain.size(); ++h) { num0 += plain[h] * plain[h] * 32.703 * (double) (h + 1); den += plain[h] * plain[h]; }
        const double c = num0 / den;
        check (c > previous * 1.05, "le spectre doit s'eclaircir avec CUT OFF (" + num (knob) + " : " + num (c) + " Hz)");
        previous = c;

        // Pic de résonance : l'harmonique la plus renforcée par la résonance ; il monte avec CUT OFF et dépasse +6 dB
        double best = 0.0, bestF = 0.0;
        for (size_t h = 2; h < plain.size(); ++h)
            if (plain[h] > 1.0e-5 && resonant[h] / plain[h] > best) { best = resonant[h] / plain[h]; bestF = 32.703 * (double) (h + 1); }
        check (bestF > previousPeak * 1.2, "le pic de resonance doit monter avec CUT OFF (" + num (knob) + " : " + num (bestF) + " Hz)");
        check (best > 2.0, "resonance a fond : pic de +6 dB au moins (" + num (20.0 * std::log10 (best)) + " dB)");
        previousPeak = bestF;
    }

    // Plus de résonance = pic plus marqué et basses plus faibles (signature de l'échelle de diodes)
    {
        AcidVoice a, b;
        a.prepare (rate); b.prepare (rate);
        a.updateSettings (makeSettings (0.5, 0.0, 0.0, 1.0, 0.0));
        b.updateSettings (makeSettings (0.5, 1.0, 0.0, 1.0, 0.0));
        const auto outA = renderNote (a, 24, false, 800.0, 800.0, rate);
        const auto outB = renderNote (b, 24, false, 800.0, 800.0, rate);
        const double lowA = toneLevel (outA, (size_t) (0.3 * rate), (size_t) (0.4 * rate), 65.4, rate);
        const double lowB = toneLevel (outB, (size_t) (0.3 * rate), (size_t) (0.4 * rate), 65.4, rate);
        check (lowB < lowA, "la resonance doit creuser les basses : " + num (lowA) + " -> " + num (lowB));
    }

    // Pas d'auto-oscillation : après la note, la sortie retombe à zéro, même résonance à fond
    for (double knob : { 0.0, 0.5, 1.0 })
    {
        AcidVoice v;
        v.prepare (rate);
        v.updateSettings (makeSettings (knob, 1.0, 1.0, 1.0, 1.0));
        auto out = renderNote (v, 36, true, 100.0, 1500.0, rate);
        check (peakOf (out, (size_t) (1.2 * rate)) < 1.0e-4, "auto-oscillation a resonance max (cutoff " + num (knob) + ")");
        check (v.isIdle(), "la voix doit se mettre au repos apres la note");
    }
}

static void testLevelsAndStability()
{
    std::cout << "- niveaux, ecretage, stabilite (reglages au hasard, 22 a 192 kHz)" << std::endl;

    // Niveau d'une note normale vers -12 dBFS ; accent à fond nettement plus fort mais jamais au-dessus de 1
    {
        AcidVoice v;
        v.prepare (44100.0);
        v.updateSettings (makeSettings (0.5, 0.5, 0.5, 0.5, 1.0));
        const auto normal = renderNote (v, 36, false, 60.0, 200.0, 44100.0);
        v.kill();
        const auto accented = renderNote (v, 36, true, 60.0, 200.0, 44100.0);
        const double pn = peakOf (normal), pa = peakOf (accented);
        check (pn > 0.15 && pn < 0.4, "crete d'une note normale : " + num (pn));
        check (pa > 2.0 * pn && pa <= 1.0, "crete d'un accent : " + num (pa) + " (normale " + num (pn) + ")");

        v.updateSettings (makeSettings (0.5, 0.5, 0.5, 0.5, 0.0));
        v.kill();
        const auto flat = renderNote (v, 36, true, 60.0, 200.0, 44100.0);
        check (peakOf (flat) < 1.35 * pn, "ACCENT a zero : un accent ne doit presque pas etre plus fort");
    }

    std::mt19937 rng (303);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    for (double rate : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        AcidVoice v;
        v.prepare (rate);
        bool finite = true, bounded = true;
        for (int round = 0; round < 40; ++round)
        {
            auto s = makeSettings (u (rng), u (rng), u (rng), u (rng), u (rng), u (rng) > 0.5);
            s.tuning = (u (rng) - 0.5) * 24.0;
            s.slideMs = 10.0 + 290.0 * u (rng);
            if (round % 7 == 0) s = makeSettings (1.0, 1.0, 1.0, 1.0, 1.0, round % 2 == 0);
            v.updateSettings (s);
            v.noteOn (12 + (int) (u (rng) * 80.0), u (rng) > 0.5, u (rng) > 0.6);
            const int len = (int) (rate * (0.02 + 0.2 * u (rng)));
            for (int i = 0; i < len; ++i)
            {
                if (i == len / 2 && u (rng) > 0.3) v.noteOff();
                const double y = v.process();
                finite = finite && std::isfinite (y);
                bounded = bounded && std::abs (y) <= 1.0;
            }
        }
        check (finite, "sortie non finie a " + num (rate));
        check (bounded, "sortie au-dela de 1 a " + num (rate));
    }
}

static void testClicks()
{
    std::cout << "- pas de clic a l'attaque et au relachement" << std::endl;
    const double rate = 48000.0;
    AcidVoice v;
    v.prepare (rate);
    v.updateSettings (makeSettings (0.2, 0.3, 0.2, 0.5, 0.5));

    // Note grave, filtre fermé : la forme d'onde est lisse ; un saut d'un échantillon à l'autre trahirait un clic
    const auto out = renderNote (v, 36, false, 200.0, 400.0, rate);
    double maxJump = 0.0;
    for (size_t i = 1; i < out.size(); ++i)
        maxJump = std::max (maxJump, std::abs (out[i] - out[i - 1]));
    check (maxJump < 0.05, "saut maximal entre deux echantillons : " + num (maxJump));
    check (std::abs (out.front()) < 1.0e-3, "le premier echantillon doit partir de zero");
}

static void testAccentSweep()
{
    std::cout << "- circuit d'accent : accumulation a resonance haute" << std::endl;
    const double rate = 44100.0;

    auto peaks = [&] (double res)
    {
        AcidVoice v;
        v.prepare (rate);
        v.updateSettings (makeSettings (0.2, res, 0.0, 0.5, 1.0));
        std::vector<double> perNote;
        const int step = (int) (rate * 60.0 / 140.0 / 4.0);   // 1/16 à 140 BPM
        for (int n = 0; n < 6; ++n)
        {
            v.noteOn (36, true, false);
            double top = 0.0;
            for (int i = 0; i < step; ++i)
            {
                if (i == step / 2) v.noteOff();
                v.process();
                top = std::max (top, v.getCurrentCutoff());
            }
            perNote.push_back (top);
        }
        return perNote;
    };

    const auto high = peaks (1.0), low = peaks (0.0);
    check (high.back() > high.front() * 1.08, "resonance haute : les accents rapproches doivent monter (" + num (high.front()) + " -> " + num (high.back()) + " Hz)");
    check (low.back() < low.front() * 1.08, "resonance basse : pas d'accumulation (" + num (low.front()) + " -> " + num (low.back()) + " Hz)");
    check (high.back() > low.back(), "l'accent doit ouvrir plus a resonance haute");
}

static void testSlide()
{
    std::cout << "- slide : glissement exponentiel sans nouvelle attaque" << std::endl;
    const double rate = 48000.0;
    AcidVoice v;
    v.prepare (rate);
    v.updateSettings (makeSettings (0.6, 0.2, 0.5, 0.5, 0.5));

    v.noteOn (36, false, false);
    for (int i = 0; i < (int) (0.1 * rate); ++i) v.process();
    const double before = v.getFrequency();

    v.noteOn (48, false, true);   // legato : glisse
    v.process();
    check (std::abs (v.getFrequency() - before) < 0.05 * before, "le slide ne doit pas sauter d'un coup");

    for (int i = 0; i < (int) (0.06 * rate); ++i) v.process();
    check (std::abs (v.getFrequency() / 130.81 - 1.0) < 0.02, "60 ms apres : quasiment arrive (" + num (v.getFrequency()) + " Hz)");

    // Sans gate (note relâchée), un legato redéclenche normalement
    v.noteOff();
    for (int i = 0; i < (int) (0.5 * rate); ++i) v.process();
    v.noteOn (36, false, true);
    v.process();
    check (std::abs (v.getFrequency() / 65.406 - 1.0) < 0.001, "legato sans note tenue : la note part directement");
}

static void testSquare()
{
    std::cout << "- carre 303 (dent de scie saturee)" << std::endl;
    const double rate = 96000.0;
    AcidVoice v;
    v.prepare (rate);
    v.updateSettings (makeSettings (1.0, 0.0, 0.0, 1.0, 0.0, true));
    const auto out = renderNote (v, 33, false, 600.0, 600.0, rate);   // 55 Hz

    // Rapport cyclique : part du temps au-dessus de la moyenne, proche de la moitié mais pas exactement
    size_t above = 0, count = 0;
    double mean = 0.0;
    for (size_t i = (size_t) (0.2 * rate); i < (size_t) (0.5 * rate); ++i) mean += out[i], ++count;
    mean /= (double) count;
    for (size_t i = (size_t) (0.2 * rate); i < (size_t) (0.5 * rate); ++i) above += out[i] > mean;
    const double duty = (double) above / (double) count;
    check (duty > 0.38 && duty < 0.62, "rapport cyclique du carre : " + num (duty));

    // Spectre : harmoniques impaires dominantes, paires présentes mais faibles (carré imparfait)
    const double h1 = toneLevel (out, (size_t) (0.2 * rate), (size_t) (0.3 * rate), 55.0, rate);
    const double h2 = toneLevel (out, (size_t) (0.2 * rate), (size_t) (0.3 * rate), 110.0, rate);
    const double h3 = toneLevel (out, (size_t) (0.2 * rate), (size_t) (0.3 * rate), 165.0, rate);
    check (h3 > 2.0 * h2, "carre : harmonique 3 > harmonique 2 (" + num (h3) + " / " + num (h2) + ")");
    check (h2 > 0.005 * h1, "carre 303 : un peu d'harmoniques paires");
}

//==============================================================================
struct Logged { int offset; AcidSequencer::EventType type; int note; bool accent; long long absolute; };

static std::vector<Logged> runSequencer (const Pattern& p, double bpm, double rate, double seconds, int blockSize, double swing = 0.0,
                                         double startPpq = 0.0, int transpose = 0, double stepBeats = 0.25, bool randomBlocks = false)
{
    AcidSequencer seq;
    seq.setPattern (p);
    seq.setSwing (swing);
    seq.setTranspose (transpose);
    seq.setStepBeats (stepBeats);
    std::vector<Logged> log;
    std::mt19937 rng (7);
    long long done = 0;
    const long long total = (long long) (seconds * rate);
    while (done < total)
    {
        int n = randomBlocks ? 1 + (int) (rng() % 700) : blockSize;
        n = (int) std::min<long long> (n, total - done);
        const double ppq = startPpq + (double) done * bpm / 60.0 / rate;
        seq.process (true, ppq, bpm, rate, n, [&] (int o, const AcidSequencer::Event& e) { log.push_back ({ o, e.type, e.note, e.accent, done + o }); });
        done += n;
    }
    return log;
}

static void testSequencer()
{
    std::cout << "- sequenceur : pas, gate, slide, tie, rest, swing, longueur, transposition" << std::endl;
    using T = AcidSequencer::EventType;
    const double rate = 48000.0, bpm = 120.0;
    const double stepSamples = 60.0 / bpm / 4.0 * rate;   // 6000

    // 16 notes simples : une attaque par pas, gate à la moitié
    {
        Pattern p;
        const auto log = runSequencer (p, bpm, rate, 2.0, 512);
        int triggers = 0, releases = 0;
        for (const auto& e : log)
        {
            if (e.type == T::trigger)
            {
                check (std::llabs (e.absolute - (long long) std::llround (triggers * stepSamples)) <= 1, "attaque du pas " + std::to_string (triggers) + " a " + std::to_string (e.absolute));
                check (e.note == AcidSequencer::baseNote, "note du pas");
                ++triggers;
            }
            if (e.type == T::release)
            {
                check (std::llabs (e.absolute - (long long) std::llround (releases * stepSamples + 0.5 * stepSamples)) <= 1, "gate a la moitie du pas");
                ++releases;
            }
        }
        check (triggers == 16, "16 attaques en 2 s a 120 BPM : " + std::to_string (triggers));
    }

    // Slide : pas 1 avec slide vers pas 2 -> pas de release entre les deux, pas 2 = slide
    {
        Pattern p;
        p.steps[0].slide = true;
        p.steps[1].pitch = 7;
        p.steps[2].time = Step::tie;
        p.steps[3].time = Step::rest;
        p.steps[5].accent = true;
        p.steps[5].octave = 2;
        p.length = 8;
        const auto log = runSequencer (p, bpm, rate, 1.0, 256);
        std::vector<Logged> e = log;
        check (e.size() >= 5, "evenements du motif");
        if (e.size() >= 5)
        {
            check (e[0].type == T::trigger && e[0].absolute == 0, "pas 1 : attaque");
            check (e[1].type == T::slide && e[1].note == AcidSequencer::baseNote + 7 && std::llabs (e[1].absolute - 6000) <= 1, "pas 2 : slide vers G");
            // pas 3 = TIE : la note continue, release à la moitié du pas 3 (le pas 4 est un silence)
            check (e[2].type == T::release && std::llabs (e[2].absolute - 15000) <= 1, "TIE puis release a la moitie du pas 3 : " + std::to_string (e[2].absolute));
            check (e[3].type == T::trigger && std::llabs (e[3].absolute - 24000) <= 1, "pas 5 : attaque");
            const auto accented = std::find_if (e.begin(), e.end(), [] (const Logged& x) { return x.accent; });
            check (accented != e.end() && std::llabs (accented->absolute - 30000) <= 1 && accented->note == AcidSequencer::baseNote + 12, "pas 6 : accent, octave UP");
        }
    }

    // Indépendance vis-à-vis de la taille des blocs
    {
        Pattern p;
        for (size_t i = 0; i < 16; ++i)
        {
            p.steps[i].pitch = (int) (i * 5 % 13);
            p.steps[i].slide = i % 3 == 0;
            p.steps[i].time = i % 5 == 4 ? Step::rest : (i % 7 == 6 ? Step::tie : Step::note);
            p.steps[i].accent = i % 4 == 1;
        }
        const auto a = runSequencer (p, 137.0, 44100.0, 4.0, 64, 0.4);
        const auto b = runSequencer (p, 137.0, 44100.0, 4.0, 0, 0.4, 0.0, 0, 0.25, true);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same = a[i].absolute == b[i].absolute && a[i].type == b[i].type && a[i].note == b[i].note;
        check (same, "les evenements doivent etre les memes quelle que soit la taille des blocs");
    }

    // Swing : les pas pairs (2, 4…) sont retardés d'un tiers de pas à 100 %
    {
        Pattern p;
        const auto log = runSequencer (p, bpm, rate, 0.5, 480, 1.0);
        std::vector<long long> starts;
        for (const auto& e : log) if (e.type == T::trigger) starts.push_back (e.absolute);
        check (starts.size() >= 4 && std::llabs (starts[1] - 8000) <= 1 && std::llabs (starts[2] - 12000) <= 1, "swing a 100 %");
    }

    // Longueur et transposition, départ au milieu d'une mesure
    {
        Pattern p;
        for (size_t i = 0; i < 16; ++i) p.steps[i].pitch = (int) i % 13;
        p.length = 3;
        const auto log = runSequencer (p, bpm, rate, 1.0, 333, 0.0, 1.0, 5);   // départ au temps 2 (pas 4)
        std::vector<int> notes;
        for (const auto& e : log) if (e.type == T::trigger) notes.push_back (e.note - AcidSequencer::baseNote - 5);
        // pas absolus 4, 5, 6… -> index 4 % 3 = 1, 2, 0, 1…
        check (notes.size() >= 4 && notes[0] == 1 && notes[1] == 2 && notes[2] == 0 && notes[3] == 1, "motif de 3 pas, depart au temps 2, transpose +5");
    }

    // Saut de position (boucle de l'hôte) : release puis reprise propre
    {
        AcidSequencer seq;
        Pattern p;
        p.steps[0].slide = true;   // gate tenu au moment du saut
        seq.setPattern (p);
        std::vector<Logged> log;
        auto cb = [&] (int o, const AcidSequencer::Event& e) { log.push_back ({ o, e.type, e.note, e.accent, o }); };
        seq.process (true, 0.0, bpm, rate, 4000, cb);
        log.clear();
        seq.process (true, 8.0, bpm, rate, 4000, cb);   // la boucle repart ailleurs
        check (! log.empty() && log.front().type == T::release && log.front().offset == 0, "release au saut de position");
        check (log.size() >= 2 && log[1].type == T::trigger && log[1].offset == 0, "reprise au pas qui commence");
        log.clear();
        seq.process (false, 0.0, bpm, rate, 512, cb);
        check (! log.empty() && log.front().type == T::release, "arret du transport : release");
        check (seq.getCurrentStep() == -1, "pas en cours a l'arret");
    }

    // Triolets et 1/32
    {
        Pattern p;
        const auto t = runSequencer (p, bpm, rate, 0.5, 256, 0.0, 0.0, 0, 1.0 / 6.0);
        const auto d = runSequencer (p, bpm, rate, 0.5, 256, 0.0, 0.0, 0, 0.125);
        int tc = 0, dc = 0;
        for (const auto& e : t) tc += e.type == T::trigger;
        for (const auto& e : d) dc += e.type == T::trigger;
        check (tc == 6 && dc == 8, "1/16T et 1/32 : " + std::to_string (tc) + " / " + std::to_string (dc));
    }
}

static void testFactoryPresets()
{
    std::cout << "- banque d'usine" << std::endl;
    const auto& list = factoryPresets();
    check (std::string (list[0].name) == "INIT", "le premier preset est INIT");

    int minor = 0, major = 0, acidCount = 0;
    for (const auto& p : list)
    {
        const std::string name = p.name;
        minor += name.rfind ("MINOR", 0) == 0;
        major += name.rfind ("MAJOR", 0) == 0;
        acidCount += name.rfind ("ACID", 0) == 0;

        check (p.cutoff >= 0 && p.cutoff <= 100 && p.resonance >= 0 && p.resonance <= 100 && p.accent >= 0 && p.accent <= 100, "potards hors plage : " + name);
        check (p.length >= 1 && p.length <= 16 && p.rate >= 0 && p.rate <= 3, "longueur / vitesse : " + name);

        const auto pattern = parsePattern (p);
        int notes = 0;
        for (const auto& s : pattern.steps)
        {
            check (s.pitch >= 0 && s.pitch <= 12 && s.octave >= 0 && s.octave <= 2, "pas illisible : " + name);
            notes += s.time == Step::note;
        }
        check (notes >= 8, "motif trop vide : " + name);
    }
    check (minor >= 5 && major >= 4 && acidCount >= 8, "categories de la banque");

    // Lecture des pas
    const auto s1 = parseStep ("A#v~");
    check (s1.pitch == 10 && s1.octave == 0 && s1.slide && ! s1.accent && s1.time == Step::note, "lecture de A#v~");
    const auto s2 = parseStep ("C'!");
    check (s2.pitch == 12 && s2.accent && s2.octave == 1, "lecture de C'!");
    const auto s3 = parseStep ("D#^!");
    check (s3.pitch == 3 && s3.octave == 2 && s3.accent, "lecture de D#^!");
    check (parseStep ("-").time == Step::rest && parseStep ("=").time == Step::tie, "lecture de - et =");

    // Les gammes : un motif MINOR ne contient pas de tierce majeure (E), un MAJOR pas de tierce mineure (D#)
    for (const auto& p : list)
    {
        const std::string name = p.name;
        const auto pattern = parsePattern (p);
        bool hasE = false, hasDs = false;
        for (const auto& s : pattern.steps)
            if (s.time == Step::note) { hasE = hasE || s.pitch == 4; hasDs = hasDs || s.pitch == 3; }
        if (name.rfind ("MINOR", 0) == 0) check (! hasE, "tierce majeure dans " + name);
        if (name.rfind ("MAJOR", 0) == 0) check (! hasDs, "tierce mineure dans " + name);
    }
}

//==============================================================================
int main()
{
   #if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040);   // FTZ + DAZ, comme dans un hôte
   #endif

    std::cout << "Tests du moteur Acid Machine" << std::endl;
    testDecimator();
    testIdleAndPitch();
    testFilter();
    testLevelsAndStability();
    testClicks();
    testAccentSweep();
    testSlide();
    testSquare();
    testSequencer();
    testFactoryPresets();

    if (failures == 0)
        std::cout << "OK" << std::endl;
    else
        std::cout << failures << " echec(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
