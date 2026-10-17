// Outil de calage : rend une note tenue de la voix pour des réglages donnés.
// Lit des lignes "clé=valeur ..." sur l'entrée standard, écrit pour chacune
// un bloc binaire : int32 nombre d'échantillons, puis les échantillons en float32.
// Utilisé par Tools/fit303.py pour comparer la voix à des enregistrements de vraie TB-303.
//
// Clés : rate, seconds, note, gate (s), accent (0/1), cutoff, resonance, envMod, decay, accentKnob,
// square (0/1), tuning, + toutes les constantes de Calibration (cal.<nom>).

#include "../Source/AcidVoice.h"
#include <iostream>
#include <map>
#include <sstream>
#include <string>

int main()
{
    std::ios::sync_with_stdio (false);
    std::string line;

    while (std::getline (std::cin, line))
    {
        std::map<std::string, double> v;
        std::istringstream in (line);
        std::string token;
        while (in >> token)
        {
            const auto eq = token.find ('=');
            if (eq != std::string::npos)
                v[token.substr (0, eq)] = std::stod (token.substr (eq + 1));
        }

        auto get = [&] (const char* k, double d) { auto it = v.find (k); return it == v.end() ? d : it->second; };

        acid::Calibration c;
        c.inputHighpass     = get ("cal.inputHighpass", c.inputHighpass);
        c.feedbackHighpass  = get ("cal.feedbackHighpass", c.feedbackHighpass);
        c.outputAllpass     = get ("cal.outputAllpass", c.outputAllpass);
        c.outputHighpass    = get ("cal.outputHighpass", c.outputHighpass);
        c.cutoffLow         = get ("cal.cutoffLow", c.cutoffLow);
        c.cutoffHigh        = get ("cal.cutoffHigh", c.cutoffHigh);
        c.decayShort        = get ("cal.decayShort", c.decayShort);
        c.decayLong         = get ("cal.decayLong", c.decayLong);
        c.ampDecay          = get ("cal.ampDecay", c.ampDecay);
        c.ampFromFilterEnv  = get ("cal.ampFromFilterEnv", c.ampFromFilterEnv);
        c.envDepthScale     = get ("cal.envDepthScale", c.envDepthScale);
        c.ladderSaturation  = get ("cal.ladderSaturation", c.ladderSaturation);
        c.resonanceScale    = get ("cal.resonanceScale", c.resonanceScale);
        c.resonanceCurve    = get ("cal.resonanceCurve", c.resonanceCurve);
        c.inputHighpass     = get ("cal.inputHighpass", c.inputHighpass);
        c.squareDrive       = get ("cal.squareDrive", c.squareDrive);
        c.squareOffset      = get ("cal.squareOffset", c.squareOffset);
        c.squareLevel       = get ("cal.squareLevel", c.squareLevel);

        const double rate = get ("rate", 44100.0);
        acid::AcidVoice voice;
        voice.setCalibration (c);
        voice.prepare (rate);

        acid::VoiceSettings s;
        s.cutoff = get ("cutoff", 0.5);
        s.resonance = get ("resonance", 0.5);
        s.envMod = get ("envMod", 0.5);
        s.decay = get ("decay", 0.5);
        s.accent = get ("accentKnob", 0.5);
        s.square = get ("square", 0.0) > 0.5;
        s.tuning = get ("tuning", 0.0);
        voice.updateSettings (s);

        const int total = (int) (get ("seconds", 3.0) * rate);
        const int gate = (int) (get ("gate", 1.0e9) * rate);
        std::vector<float> out ((size_t) total);
        voice.noteOn ((int) get ("note", 36.0), get ("accent", 0.0) > 0.5, false);
        for (int i = 0; i < total; ++i)
        {
            if (i == gate) voice.noteOff();
            out[(size_t) i] = (float) voice.process();
        }

        const int n = total;
        std::cout.write (reinterpret_cast<const char*> (&n), 4);
        std::cout.write (reinterpret_cast<const char*> (out.data()), (std::streamsize) (sizeof (float) * out.size()));
        std::cout.flush();
    }

    return 0;
}
