#pragma once

/*
    Acid Machine — la voix (oscillateur, filtre, enveloppes, accent, slide, VCA).

    Sans dépendance (testable seul). Tout est calculé en double.

    Base : le moteur Open303 de Robin Schmidt (licence MIT, voir LICENSE.txt), dont on
    reprend les valeurs mesurées sur de vraies TB-303 par la communauté du fil KVR
    « Open303 » : correspondance cutoff / env mod, filtre « TB_303 » (équations de kunn
    et mystran, coefficients ajustés par antto), filtres d'entrée et de sortie, façonneur
    du carré, enveloppes et gain d'accent.

    Ce qui change par rapport à Open303 :
      - saturation douce à l'entrée de l'échelle (dans la boucle de résonance) : la
        résonance se tasse au lieu d'exploser quand l'accent ouvre le filtre ;
      - circuit d'accent (« accent sweep ») relié à la résonance, comme sur la machine :
        à résonance haute il est plus lent, plus ample et s'accumule d'un accent à
        l'autre ; à résonance basse il est court et faible ;
      - écrêtage doux du VCA (l'accent tape dedans) ;
      - oscillateur à polyBLEP et décimation par FIR à fenêtre de Kaiser (au lieu des
        tables et du filtre elliptique) ;
      - temps de slide réglable (60 ms = valeur d'Open303).
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace acid
{

constexpr double pi = 3.14159265358979323846;

//==============================================================================
/** Filtre du premier ordre (formules d'Open303 : passe-haut « dspguide », passe-tout « DAFX »). */
struct OnePole
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, x1 = 0.0, y1 = 0.0;

    void setHighpass (double cutoff, double sampleRate)
    {
        const double x = std::exp (-2.0 * pi * cutoff / sampleRate);
        b0 = 0.5 * (1.0 + x);
        b1 = -b0;
        a1 = x;
    }

    void setAllpass (double cutoff, double sampleRate)
    {
        const double t = std::tan (pi * cutoff / sampleRate);
        const double x = (t - 1.0) / (t + 1.0);
        b0 = x;
        b1 = 1.0;
        a1 = -x;
    }

    double process (double in) noexcept
    {
        double y = b0 * in + b1 * x1 + a1 * y1;
        if (std::abs (y) < 1.0e-25) y = 0.0;
        x1 = in;
        y1 = y;
        return y;
    }

    void reset() noexcept   { x1 = y1 = 0.0; }
};

//==============================================================================
/** Biquad (formules du « cookbook » de Robert Bristow-Johnson). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void setLowpass (double cutoff, double sampleRate, double q)
    {
        const double w = 2.0 * pi * cutoff / sampleRate;
        const double s = std::sin (w), c = std::cos (w);
        const double alpha = s / (2.0 * q);
        const double scale = 1.0 / (1.0 + alpha);
        a1 = 2.0 * c * scale;
        a2 = (alpha - 1.0) * scale;
        b1 = (1.0 - c) * scale;
        b0 = 0.5 * b1;
        b2 = b0;
    }

    void setNotch (double centre, double sampleRate, double bandwidthOctaves)
    {
        const double w = 2.0 * pi * centre / sampleRate;
        const double s = std::sin (w), c = std::cos (w);
        const double alpha = s * std::sinh (0.5 * std::log (2.0) * bandwidthOctaves * w / s);
        const double scale = 1.0 / (1.0 + alpha);
        a1 = 2.0 * c * scale;
        a2 = (alpha - 1.0) * scale;
        b0 = scale;
        b1 = -2.0 * c * scale;
        b2 = scale;
    }

    double process (double in) noexcept
    {
        double y = b0 * in + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2;
        if (std::abs (y) < 1.0e-25) y = 0.0;
        x2 = x1; x1 = in;
        y2 = y1; y1 = y;
        return y;
    }

    void reset() noexcept   { x1 = x2 = y1 = y2 = 0.0; }
};

//==============================================================================
/** Décimation par un facteur entier : FIR passe-bas à fenêtre de Kaiser, calculé une fois par échantillon de sortie. */
class Decimator
{
public:
    void prepare (int factorToUse, double outputRate)
    {
        factor = factorToUse;
        const double inputRate = outputRate * factor;
        const double passband = std::min (20000.0, 0.42 * outputRate);
        const double stopband = 0.5 * outputRate + 0.08 * outputRate;   // ce qui replie ne retombe qu'au-dessus de ~16 kHz à 44,1 kHz
        const double cutoff = 0.5 * (passband + stopband) / inputRate;
        const double transition = (stopband - passband) / inputRate;     // en fraction de la fréquence d'entrée
        const double attenuation = 90.0;
        const double beta = 0.1102 * (attenuation - 8.7);
        int taps = (int) std::ceil ((attenuation - 7.95) / (14.36 * transition)) + 1;
        taps = std::max (32, std::min (512, taps));
        taps += (taps % factor == 0 ? 0 : factor - taps % factor);

        coefficients.assign ((size_t) taps, 0.0);
        const double centre = 0.5 * (taps - 1);
        double sum = 0.0;
        for (int n = 0; n < taps; ++n)
        {
            const double t = n - centre;
            const double sinc = std::abs (t) < 1.0e-9 ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * t) / (pi * t);
            const double ratio = 2.0 * n / (taps - 1) - 1.0;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / besselI0 (beta);
            coefficients[(size_t) n] = sinc * window;
            sum += coefficients[(size_t) n];
        }
        for (auto& c : coefficients)
            c /= sum;

        history.assign ((size_t) taps * 2, 0.0);
        position = 0;
    }

    int getNumTaps() const noexcept   { return (int) coefficients.size(); }

    /** Ajoute un échantillon à la fréquence haute. */
    void push (double x) noexcept
    {
        const int taps = (int) coefficients.size();
        history[(size_t) position] = x;
        history[(size_t) (position + taps)] = x;
        position = (position + 1) % taps;
    }

    /** Sortie à la fréquence basse (après factor appels à push). */
    double output() const noexcept
    {
        const int taps = (int) coefficients.size();
        const double* h = history.data() + position;   // h[0] = le plus ancien
        double acc = 0.0;
        for (int n = 0; n < taps; ++n)
            acc += coefficients[(size_t) n] * h[n];
        return acc;
    }

    void reset() noexcept   { std::fill (history.begin(), history.end(), 0.0); position = 0; }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x * 0.5 / k) * (x * 0.5 / k);
            sum += term;
            if (term < 1.0e-14 * sum)
                break;
        }
        return sum;
    }

    int factor = 4;
    std::vector<double> coefficients, history;
    int position = 0;
};

//==============================================================================
/** Le filtre de la TB-303 : échelle à quatre étages couplés (échelle de diodes). */
class DiodeLadder
{
public:
    void prepare (double rate)
    {
        sampleRate = rate;
        feedbackHighpass.setHighpass (150.0, rate);   // valeur d'Open303
        reset();
    }

    /** resonanceSkewed : 0..1 (la résonance du potard déjà passée par la courbe d'Open303). */
    void setCoefficients (double cutoff, double resonanceSkewed) noexcept
    {
        cutoff = std::max (200.0, std::min (cutoff, std::min (20000.0, 0.15 * sampleRate)));
        const double r = resonanceSkewed;
        const double fx = cutoff / sampleRate * 0.70710678118654752;   // wc / (2 pi) / sqrt(2)

        // Ajustement de kunn / mystran / antto (fil KVR « Open303 », page 40 et suivantes)
        b0 = (0.00045522346 + 6.1922189 * fx) / (1.0 + 12.358354 * fx + 4.4156345 * (fx * fx));
        double k0 = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997) - 5837.7917) - 476.47308) + 614.95611) + 213.87126) + 16.998792;
        double gain = k0 / 17.0;
        gain = (gain - 1.0) * r + 1.0;
        gain *= (1.0 + r);
        g = gain;
        k = k0 * r;
    }

    double process (double in) noexcept
    {
        double y0 = in - feedbackHighpass.process (k * y4);

        // Saturation douce de l'entrée de l'échelle (paire différentielle). Gain 1 aux petits niveaux.
        y0 = saturationRange * std::tanh (y0 / saturationRange);

        y1 += 2.0 * b0 * (y0 - y1 + y2);
        y2 += b0 * (y1 - 2.0 * y2 + y3);
        y3 += b0 * (y2 - 2.0 * y3 + y4);
        y4 += b0 * (y3 - 2.0 * y4);

        if (std::abs (y4) < 1.0e-25) { y1 = y1 * (std::abs (y1) > 1.0e-25); y2 = y2 * (std::abs (y2) > 1.0e-25); y3 = y3 * (std::abs (y3) > 1.0e-25); y4 = 0.0; }
        return 2.0 * g * y4;
    }

    void reset() noexcept
    {
        y1 = y2 = y3 = y4 = 0.0;
        feedbackHighpass.reset();
    }

    static constexpr double saturationRange = 3.0;

private:
    double sampleRate = 176400.0;
    double b0 = 0.0, k = 0.0, g = 1.0;
    double y1 = 0.0, y2 = 0.0, y3 = 0.0, y4 = 0.0;
    OnePole feedbackHighpass;
};

//==============================================================================
/** Réglages de la voix, aux unités des potards (0..1 sauf indication). */
struct VoiceSettings
{
    double tuning = 0.0;       // demi-tons
    double cutoff = 0.5;       // potard CUT OFF FREQ
    double resonance = 0.5;
    double envMod = 0.5;
    double decay = 0.5;        // potard DECAY
    double accent = 0.5;
    bool square = false;
    double slideMs = 60.0;     // temps de slide (60 = TB-303 d'après Open303)
};

class AcidVoice
{
public:
    static constexpr int oversampling = 4;

    // Correspondances mesurées (Open303)
    static constexpr double cutoffLow = 313.8152786059267, cutoffHigh = 2394.411986817546;   // Hz, potard au minimum / maximum
    static constexpr double decayShort = 200.0, decayLong = 2000.0;                          // ms
    static constexpr double accentDecay = 200.0;                                             // ms
    static constexpr double ampDecay = 1230.0, normalRelease = 1.0, accentRelease = 50.0;    // ms

    static double cutoffForKnob (double knob)   { return cutoffLow * std::pow (cutoffHigh / cutoffLow, clamp01 (knob)); }
    static double decayForKnob (double knob)    { return decayShort * std::pow (decayLong / decayShort, clamp01 (knob)); }
    static double skewResonance (double knob)   { return (1.0 - std::exp (-3.0 * clamp01 (knob))) / (1.0 - std::exp (-3.0)); }

    void prepare (double rate)
    {
        sampleRate = rate;
        const double high = rate * oversampling;

        inputHighpass.setHighpass (44.486, high);
        ladder.prepare (high);
        decimator.prepare (oversampling, rate);

        outputAllpass.setAllpass (14.008, rate);
        outputHighpass.setHighpass (24.167, rate);
        outputNotch.setNotch (7.5164, rate, 4.7);
        ampSmoother.setLowpass (200.0, rate, std::sqrt (0.5));

        updateSettings (settings);
        resetState();
    }

    void updateSettings (const VoiceSettings& s) noexcept
    {
        settings = s;
        cutoffHz = cutoffForKnob (s.cutoff);
        resonanceSkewed = skewResonance (s.resonance);

        // Correspondance cutoff / env mod mesurée (Open303)
        const double e = clamp01 (s.envMod);
        const double c = clamp01 (s.cutoff);
        const double sLo = 3.773996325111173 * e + 0.736965594166206;
        const double sHi = 4.194548788411135 * e + 0.864344900642434;
        envScaler = (1.0 - c) * sLo + c * sHi;
        envOffset = 0.048292930943553 * c + 0.294391201442418;

        normalDecay = decayForKnob (s.decay);
        slideCoeff = std::exp (-1.0 / (0.001 * 0.2 * std::max (1.0, s.slideMs) * sampleRate));

        // Circuit d'accent : la résonance (potard double) règle sa vitesse et sa part dans le cutoff
        const double rs = resonanceSkewed;
        sweepCharge = std::exp (-1.0 / (0.001 * (2.0 + 18.0 * rs) * sampleRate));
        sweepDischarge = std::exp (-1.0 / (0.001 * (15.0 + 225.0 * rs) * sampleRate));
        sweepDepth = 0.35 + 0.65 * rs;

        updateDecays();
    }

    const VoiceSettings& getSettings() const noexcept   { return settings; }

    /** Nouvelle note : attaque de l'enveloppe, sauf si legato (slide : la hauteur glisse, rien n'est redéclenché). */
    void noteOn (int midiNote, bool accented, bool legato) noexcept
    {
        targetFrequency = 440.0 * std::pow (2.0, (midiNote - 69 + settings.tuning) / 12.0);
        accent = accented;
        updateDecays();

        if (legato && gateOn)
            return;

        if (idle)
        {
            // Rien ne sonne : on repart de zéro (pas de clic possible)
            phase = 0.0;
            inputHighpass.reset();
            ladder.reset();
            decimator.reset();
            outputAllpass.reset();
            outputHighpass.reset();
            outputNotch.reset();
            ampSmoother.reset();
        }

        frequency = targetFrequency;
        mainEnv = 1.0;
        ampEnv = 1.0;
        gateOn = true;
        idle = false;
    }

    /** Retour sur une note encore tenue (MIDI) : glisse vers elle sans redéclencher. */
    void glideTo (int midiNote) noexcept
    {
        targetFrequency = 440.0 * std::pow (2.0, (midiNote - 69 + settings.tuning) / 12.0);
    }

    void noteOff() noexcept
    {
        gateOn = false;
    }

    /** Coupe tout (arrêt du transport, bypass). */
    void kill() noexcept
    {
        gateOn = false;
        ampEnv = 0.0;
        mainEnv = 0.0;
        sweep = 0.0;
        resetState();
    }

    bool isIdle() const noexcept        { return idle; }
    bool isGateOn() const noexcept      { return gateOn; }
    double getFrequency() const noexcept { return frequency; }
    double getCurrentCutoff() const noexcept { return currentCutoff; }

    /** Un échantillon à la fréquence de l'hôte. */
    double process() noexcept
    {
        if (idle)
            return 0.0;

        // Hauteur : slide exponentiel
        frequency = targetFrequency + (frequency - targetFrequency) * slideCoeff;

        // Enveloppe du filtre (MEG) et circuit d'accent
        mainEnv *= mainCoeff;
        const double accentInput = accent ? mainEnv : 0.0;
        sweep = accentInput + (sweep - accentInput) * (accentInput > sweep ? sweepCharge : sweepDischarge);

        const double octaves = envScaler * (mainEnv - envOffset) + settings.accent * sweepDepth * sweep;
        currentCutoff = cutoffHz * std::exp2 (octaves);
        ladder.setCoefficients (currentCutoff, resonanceSkewed);

        // VCA (VEG) : décroissance lente tant que le gate est tenu, relâchement court ensuite
        ampEnv *= gateOn ? ampDecayCoeff : (accent ? accentReleaseCoeff : normalReleaseCoeff);
        double amp = ampEnv;
        if (gateOn)
            amp += 0.45 * mainEnv + (accent ? settings.accent * 4.0 : 0.0) * mainEnv;
        amp = ampSmoother.process (amp);

        // Oscillateur + filtre, suréchantillonnés
        const double increment = frequency / (sampleRate * oversampling);
        for (int i = 0; i < oversampling; ++i)
        {
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            double saw = 2.0 * phase - 1.0;
            saw -= polyBlep (phase, increment);   // dent de scie montante, front de retour adouci

            // Carré « 303 » : la dent de scie passée dans un étage saturé (Open303 : tanh, +36,9 dB, décalage 4,37),
            // à mi-niveau comme dans Open303 (le carré a deux fois plus d'énergie que la dent de scie)
            const double wave = settings.square ? -0.5 * std::tanh (squareDrive * saw + squareOffset) : saw;

            double x = inputHighpass.process (-wave);
            x = ladder.process (x);
            decimator.push (x);
        }

        double y = decimator.output();
        y = outputAllpass.process (y);
        y = outputHighpass.process (y);
        y = outputNotch.process (y);
        y *= amp * outputScale;

        // Écrêtage doux du VCA : l'accent y tape (la 303 sature vers 85 % de sa plage)
        y = softClip (y);

        if (! gateOn && ampEnv < 1.0e-6 && std::abs (y) < 1.0e-6)
            kill(), idle = true;

        return y;
    }

    void resetState() noexcept
    {
        phase = 0.0;
        inputHighpass.reset();
        ladder.reset();
        decimator.reset();
        outputAllpass.reset();
        outputHighpass.reset();
        outputNotch.reset();
        ampSmoother.reset();
        idle = true;
    }

    /** Niveau de sortie : une note normale culmine vers -12 dBFS, un accent à fond juste sous 0 dBFS
        (l'écart d'Open303 : jusqu'à +11 dB). L'écrêtage ne prend que les crêtes de résonance. */
    static constexpr double outputScale = 0.21;
    static constexpr double squareDrive = 70.0;          // dB2amp (36.9)
    static constexpr double squareOffset = 4.37;

    static double softClip (double x) noexcept
    {
        // Linéaire jusqu'à 0,85, puis courbe douce jusqu'à 1
        constexpr double knee = 0.85;
        const double a = std::abs (x);
        if (a <= knee)
            return x;
        const double over = (a - knee) / (1.0 - knee);
        return std::copysign (knee + (1.0 - knee) * std::tanh (over), x);
    }

private:
    static double clamp01 (double v) noexcept   { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    static double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    void updateDecays() noexcept
    {
        const double decayMs = accent ? accentDecay : normalDecay;
        mainCoeff = std::exp (-1.0 / (0.001 * decayMs * sampleRate));
        ampDecayCoeff = std::exp (-1.0 / (0.001 * ampDecay * sampleRate));
        normalReleaseCoeff = std::exp (-1.0 / (0.001 * normalRelease * sampleRate));
        accentReleaseCoeff = std::exp (-1.0 / (0.001 * accentRelease * sampleRate));
    }

    double sampleRate = 44100.0;
    VoiceSettings settings;

    double cutoffHz = 1000.0, resonanceSkewed = 0.0, envScaler = 1.0, envOffset = 0.3, normalDecay = 600.0;
    double currentCutoff = 1000.0;
    double sweepCharge = 0.0, sweepDischarge = 0.0, sweepDepth = 1.0, sweep = 0.0;
    double mainCoeff = 0.0, ampDecayCoeff = 0.0, normalReleaseCoeff = 0.0, accentReleaseCoeff = 0.0, slideCoeff = 0.0;

    double phase = 0.0, frequency = 110.0, targetFrequency = 110.0;
    double mainEnv = 0.0, ampEnv = 0.0;
    bool accent = false, gateOn = false, idle = true;

    OnePole inputHighpass, outputAllpass, outputHighpass;
    Biquad outputNotch, ampSmoother;
    DiodeLadder ladder;
    Decimator decimator;
};

} // namespace acid
