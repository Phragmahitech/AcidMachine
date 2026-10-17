// Tests du plugin complet : paramètres, MIDI, séquenceur calé sur l'hôte, presets d'usine,
// presets utilisateur, état, bypass, interface (dont la grille des pas) et taille de la fenêtre.

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/FactoryPresets.h"

#include <iostream>
#include <random>

// Les tests comparent des valeurs entières de paramètres (pas, octave…) stockées en float
#if defined(__clang__) || defined(__GNUC__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        ++failures;
        std::cout << "  ECHEC : " << what << std::endl;
    }
}

#include "PresetTests.h"

using Proc = AcidMachineAudioProcessor;

//==============================================================================
/** Hôte simulé : transport, tempo, position. */
struct FakePlayHead final : public juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 120.0, ppq = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

static void setParam (juce::AudioProcessor& p, const juce::String& id, float value)
{
    auto& proc = dynamic_cast<Proc&> (p);
    if (auto* param = proc.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else
        check (false, "parametre absent : " + id);
}

static float getParam (juce::AudioProcessor& p, const juce::String& id)
{
    auto& proc = dynamic_cast<Proc&> (p);
    auto* param = proc.apvts.getParameter (id);
    return param != nullptr ? param->convertFrom0to1 (param->getValue()) : -1.0e9f;
}

/** Rend des blocs ; l'hôte simulé avance si son transport tourne. */
static std::vector<float> render (juce::AudioProcessor& p, FakePlayHead& head, double rate, int blocks, int blockSize,
                                  std::function<void (int, juce::MidiBuffer&)> midiForBlock = {})
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (midiForBlock) midiForBlock (b, midi);
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
        check (std::memcmp (buffer.getReadPointer (0), buffer.getReadPointer (1), sizeof (float) * (size_t) blockSize) == 0, "canaux gauche et droit differents");
        if (head.playing)
            head.ppq += head.bpm / 60.0 * blockSize / rate;
    }
    return out;
}

static float peak (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    float m = 0.0f;
    for (size_t i = from; i < std::min (to, x.size()); ++i) m = std::max (m, std::abs (x[i]));
    return m;
}

static double pitchOf (const std::vector<float>& x, size_t from, size_t count, double rate)
{
    double a = 0.0, b = 0.0;
    std::vector<double> y (count);
    for (size_t i = 0; i < count; ++i) { a += 0.02 * (x[from + i] - a); b += 0.02 * (a - b); y[i] = b; }
    double mean = 0.0;
    for (auto v : y) mean += v;
    mean /= (double) count;
    std::vector<double> c;
    for (size_t i = count / 4; i + 1 < count; ++i)
        if (y[i] - mean <= 0.0 && y[i + 1] - mean > 0.0)
            c.push_back ((double) i + (mean - y[i]) / (y[i + 1] - y[i]));
    return c.size() < 3 ? 0.0 : rate * (double) (c.size() - 1) / (c.back() - c.front());
}

//==============================================================================
static void testParameters()
{
    std::cout << "- parametres" << std::endl;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*base);

    check (p.getParameters().size() == 14 + 5 * Proc::numSteps, "nombre de parametres : " + juce::String (p.getParameters().size()));

    juce::StringArray ids;
    for (auto* param : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ids.addIfNotAlreadyThere (ranged->getParameterID());
    check (ids.size() == p.getParameters().size(), "identifiants en double");

    for (const char* id : { Proc::tuningId, Proc::cutoffId, Proc::resonanceId, Proc::envModId, Proc::decayId, Proc::accentId,
                            Proc::waveformId, Proc::volumeId, Proc::slideTimeId, Proc::seqId, Proc::lengthId, Proc::rateId,
                            Proc::swingId, Proc::transposeId })
        check (ids.contains (id), juce::String ("parametre manquant : ") + id);

    check (ids.contains ("s1Pitch") && ids.contains ("s16Slide"), "parametres des pas");
    check (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "instrument a entree MIDI");
    check (getParam (p, Proc::seqId) > 0.5f, "SEQ allume par defaut");

    // Textes
    auto text = [&] (const juce::String& id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
    setParam (p, "s3Pitch", 10.0f);
    check (text ("s3Pitch") == "A#", "nom de note : " + text ("s3Pitch"));
    setParam (p, Proc::transposeId, -5.0f);
    check (text (Proc::transposeId) == "-5 st", "texte de TRANSPOSE : " + text (Proc::transposeId));
    check (text (Proc::cutoffId) == "50 %", "texte de CUT OFF : " + text (Proc::cutoffId));
}

static void testFactoryBank()
{
    std::cout << "- banque d'usine (INIT, MINOR, MAJOR, ACID)" << std::endl;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*base);

    check (p.presets.getDisplayName() == "INIT", "plugin neuf : " + p.presets.getDisplayName());
    check (p.matchingFactoryPreset() == 0, "plugin neuf = INIT");

    const auto& list = acid::factoryPresets();
    for (int i = (int) list.size() - 1; i >= 0; --i)
    {
        p.presets.loadFactory (i);
        check (p.matchingFactoryPreset() == i, juce::String ("preset non reconnu apres chargement : ") + list[(size_t) i].name);
        check (p.presets.getDisplayName() == list[(size_t) i].name && ! p.presets.isModified(), juce::String ("nom / etoile : ") + list[(size_t) i].name);

        const auto expected = acid::parsePattern (list[(size_t) i]);
        const auto actual = p.readPattern();
        bool same = expected.length == actual.length;
        for (size_t s = 0; same && s < 16; ++s)
        {
            const auto& a = expected.steps[s];
            const auto& b = actual.steps[s];
            same = a.pitch == b.pitch && a.octave == b.octave && a.time == b.time && a.accent == b.accent && a.slide == b.slide;
        }
        check (same, juce::String ("motif mal applique : ") + list[(size_t) i].name);
    }

    // Changer un pas fait apparaître l'étoile
    p.presets.loadFactory (3);
    setParam (p, "s5Accent", getParam (p, "s5Accent") > 0.5f ? 0.0f : 1.0f);
    check (p.presets.isModified(), "pas modifie : etoile absente");
    check (p.matchingFactoryPreset() == -1, "un motif modifie ne doit plus correspondre");
}

static void testMidiMode()
{
    std::cout << "- mode MIDI (SEQ eteint) : notes, accent par velocite, slide par chevauchement" << std::endl;
    const double rate = 48000.0;
    const int block = 480;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*base);
    FakePlayHead head;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);

    setParam (p, Proc::seqId, 0.0f);
    setParam (p, Proc::accentId, 100.0f);
    setParam (p, Proc::cutoffId, 80.0f);
    setParam (p, Proc::resonanceId, 20.0f);

    // Silence sans note
    auto quiet = render (p, head, rate, 20, block);
    check (peak (quiet) == 0.0f, "silence sans note");

    // Note normale (vélocité 100) puis accent (127)
    auto normal = render (p, head, rate, 20, block, [] (int b, juce::MidiBuffer& m)
    {
        if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
        if (b == 5) m.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    });
    check (peak (normal) > 0.05f, "une note MIDI doit sonner");
    check (peak (normal, (size_t) (0.15 * rate)) < 1.0e-4f, "la note doit s'eteindre apres le note off");
    check (normal[9] == 0.0f && normal[11] != 0.0f, "la note part a l'echantillon du note on");

    auto accented = render (p, head, rate, 20, block, [] (int b, juce::MidiBuffer& m)
    {
        if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 10);
        if (b == 5) m.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    });
    check (peak (accented) > 1.8f * peak (normal), "velocite 127 = accent : " + juce::String (peak (accented)) + " contre " + juce::String (peak (normal)));

    // Deux notes qui se chevauchent : glissé vers la seconde, retour sur la première quand on lâche la seconde
    setParam (p, Proc::decayId, 100.0f);
    auto slid = render (p, head, rate, 100, block, [] (int b, juce::MidiBuffer& m)
    {
        if (b == 0)  m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        if (b == 30) m.addEvent (juce::MidiMessage::noteOn (1, 43, (juce::uint8) 100), 0);
        if (b == 60) m.addEvent (juce::MidiMessage::noteOff (1, 43), 0);
        if (b == 90) m.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    });
    const double f1 = pitchOf (slid, 3000, 11000, rate);
    const double f2 = pitchOf (slid, 30 * block + 3000, 11000, rate);
    const double f3 = pitchOf (slid, 60 * block + 3000, 11000, rate);
    check (std::abs (f1 / 65.406 - 1.0) < 0.01, "premiere note : " + juce::String (f1));
    check (std::abs (f2 / 97.999 - 1.0) < 0.01, "seconde note (glissee) : " + juce::String (f2));
    check (std::abs (f3 / 65.406 - 1.0) < 0.01, "retour sur la note tenue : " + juce::String (f3));

    // Pas de nouvelle attaque au chevauchement : le niveau ne remonte pas d'un coup
    const float before = peak (slid, 29 * block, 30 * block);
    const float after = peak (slid, 30 * block, 31 * block + 400);
    check (after < 1.25f * before, "le slide ne doit pas redeclencher l'enveloppe (" + juce::String (before) + " -> " + juce::String (after) + ")");

    p.releaseResources();
}

static void testSequencerMode()
{
    std::cout << "- mode SEQ : cale sur l'hote, transposition par note MIDI, arret du transport" << std::endl;
    const double rate = 48000.0;
    const int block = 480;   // 10 ms
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*base);
    FakePlayHead head;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);

    // Transport arrêté : rien
    auto stopped = render (p, head, rate, 30, block);
    check (peak (stopped) == 0.0f, "transport arrete : silence");
    check (p.getPlayingStep() == -1 || ! p.isSequencerRunning(), "pas de pas en cours a l'arret");

    // INIT : 16 doubles croches de C. À 120 BPM, une attaque toutes les 6000 échantillons
    head.playing = true;
    head.ppq = 0.0;
    auto playing = render (p, head, rate, 200, block);   // 2 s = 16 pas
    check (p.isSequencerRunning(), "le sequenceur doit tourner");
    int onsets = 0;
    for (size_t i = 1; i < playing.size(); ++i)
        if (playing[i - 1] == 0.0f && playing[i] != 0.0f)
        {
            check (i % 6000 == 0, "attaque hors de la grille : echantillon " + juce::String ((int) i));
            ++onsets;
        }
    check (onsets >= 15, "attaques sur la grille : " + juce::String (onsets));
    check (p.getPlayingStep() == 15, "pas en cours apres 2 s : " + juce::String (p.getPlayingStep()));

    // Une note longue (pas 2 à 16 en TIE) pour mesurer la hauteur, puis transposition par MIDI
    for (int s = 1; s < 16; ++s)
        setParam (p, Proc::stepParamId (s, "Time"), 1.0f);
    setParam (p, Proc::cutoffId, 80.0f);
    setParam (p, Proc::decayId, 100.0f);
    head.ppq = 4.0;   // saut : nouvelle mesure
    auto plain = render (p, head, rate, 150, block);
    check (std::abs (pitchOf (plain, 6000, 40000, rate) / 65.406 - 1.0) < 0.01, "note du motif : " + juce::String (pitchOf (plain, 6000, 40000, rate)));

    head.ppq = 8.0;
    auto shifted = render (p, head, rate, 150, block, [] (int b, juce::MidiBuffer& m)
    {
        if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);   // G3 = +7
    });
    check (std::abs (pitchOf (shifted, 6000, 40000, rate) / 97.999 - 1.0) < 0.01, "transposition par note MIDI : " + juce::String (pitchOf (shifted, 6000, 40000, rate)));

    // TRANSPOSE (le paramètre) s'ajoute
    setParam (p, Proc::transposeId, -2.0f);
    head.ppq = 12.0;
    auto both = render (p, head, rate, 150, block);
    check (std::abs (pitchOf (both, 6000, 40000, rate) / (65.406 * std::pow (2.0, 5.0 / 12.0)) - 1.0) < 0.01, "TRANSPOSE + note MIDI : " + juce::String (pitchOf (both, 6000, 40000, rate)));

    // Arrêt du transport : la note se coupe
    head.playing = false;
    auto after = render (p, head, rate, 30, block);
    check (peak (after, (size_t) (0.1 * rate)) < 1.0e-4f, "arret du transport : la note doit se couper");

    // Passage en mode MIDI pendant que ça joue : plus de séquence
    head.playing = true;
    setParam (p, Proc::seqId, 0.0f);
    auto midiMode = render (p, head, rate, 60, block);
    check (peak (midiMode, (size_t) (0.1 * rate)) < 1.0e-4f, "SEQ eteint : le motif ne joue plus");

    p.releaseResources();
}

static void testRobustness()
{
    std::cout << "- robustesse : frequences, tailles de bloc, reglages au hasard, mono, bypass" << std::endl;
    std::mt19937 rng (42);
    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        auto& p = dynamic_cast<Proc&> (*base);
        FakePlayHead head;
        head.playing = true;
        head.bpm = 70.0 + (double) (rng() % 130);
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, rate, 1024);
        p.prepareToPlay (rate, 1024);

        bool finite = true;
        float top = 0.0f;
        for (int round = 0; round < 60; ++round)
        {
            for (auto* param : p.getParameters())
                if (rng() % 4 == 0)
                    param->setValueNotifyingHost ((float) (rng() % 1001) / 1000.0f);
            setParam (p, Proc::volumeId, 0.0f);

            const int n = 1 + (int) (rng() % 1024);
            juce::AudioBuffer<float> buffer (2, n);
            juce::MidiBuffer midi;
            if (rng() % 3 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 20 + (int) (rng() % 80), (juce::uint8) (1 + rng() % 127)), (int) (rng() % (unsigned) n));
            if (rng() % 3 == 0) midi.addEvent (juce::MidiMessage::noteOff (1, 20 + (int) (rng() % 80)), (int) (rng() % (unsigned) n));
            p.processBlock (buffer, midi);
            head.ppq += head.bpm / 60.0 * n / rate;
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (0, i));
                top = std::max (top, std::abs (buffer.getSample (0, i)));
            }
        }
        check (finite, "sortie non finie a " + juce::String (rate));
        check (top <= 1.0f, "sortie au-dela de 0 dBFS (volume 0 dB) a " + juce::String (rate) + " : " + juce::String (top));

        // Bypass : silence, et rien ne reste en mémoire
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        p.processBlockBypassed (buffer, midi);
        check (buffer.getMagnitude (0, 256) == 0.0f, "bypass : silence");
        p.releaseResources();
    }

    // Mono
    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        juce::AudioProcessor::BusesLayout mono;
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (base->checkBusesLayoutSupported (mono), "sortie mono refusee");
        juce::AudioProcessor::BusesLayout quad;
        quad.outputBuses.add (juce::AudioChannelSet::quadraphonic());
        check (! base->checkBusesLayoutSupported (quad), "sortie quadri acceptee");
    }
}

static void testState()
{
    std::cout << "- etat du projet" << std::endl;
    std::unique_ptr<juce::AudioProcessor> a (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*a);
    p.presets.loadFactory (5);
    setParam (p, Proc::cutoffId, 63.5f);
    setParam (p, "s7Pitch", 9.0f);
    setParam (p, "s7Slide", 1.0f);

    juce::MemoryBlock state;
    p.getStateInformation (state);

    std::unique_ptr<juce::AudioProcessor> b (createPluginFilter());
    b->setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (getParam (*b, Proc::cutoffId) - 63.5f) < 0.01f, "CUT OFF non restaure");
    check (getParam (*b, "s7Pitch") == 9.0f && getParam (*b, "s7Slide") == 1.0f, "pas non restaure");
    auto& q = dynamic_cast<Proc&> (*b);
    check (q.presets.getDisplayName() == acid::factoryPresets()[5].name && q.presets.isModified(), "nom du preset et etoile non restaures : " + q.presets.getDisplayName());

    // État abîmé : ignoré
    const char junk[] = "pas un etat";
    b->setStateInformation (junk, (int) sizeof junk);
    check (std::abs (getParam (*b, Proc::cutoffId) - 63.5f) < 0.01f, "un etat abime ne doit rien changer");
}

static void testEditor (const juce::File& outputDir)
{
    std::cout << "- interface : grille des pas, potards, rendus" << std::endl;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<Proc&> (*base);
    p.presets.loadFactory (1);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
    auto* editor = dynamic_cast<AcidMachineAudioProcessorEditor*> (ed.get());
    check (editor != nullptr, "pas d'editeur");
    if (editor == nullptr)
        return;

    auto& grid = editor->getFacade().getStepGrid();
    auto centre = [&] (int column, StepGrid::Row row) { return grid.cellBounds (column, row).getCentre().toFloat(); };

    auto click = [&] (juce::Point<float> pos, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const juce::MouseEvent down (source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &grid, &grid,
                                     juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
        grid.mouseDown (down);
        grid.mouseUp (down);
    };

    // Toutes les cases sont dans la grille et ne se recouvrent pas
    for (int c = 0; c < 16; ++c)
        for (int r = 0; r < StepGrid::numRows; ++r)
        {
            const auto b = grid.cellBounds (c, (StepGrid::Row) r);
            check (grid.getLocalBounds().contains (b), "case hors de la grille");
            int col = -1;
            StepGrid::Row row = StepGrid::numberRow;
            check (grid.cellAt (b.getCentre(), col, row) && col == c && row == r, "case introuvable sous la souris");
        }

    // ACCENT, SLIDE, UP, DOWN, TIME
    const float acc = getParam (p, "s4Accent");
    click (centre (3, StepGrid::accentRow));
    check (getParam (p, "s4Accent") != acc, "clic sur ACCENT");
    const float sl = getParam (p, "s9Slide");
    click (centre (8, StepGrid::slideRow));
    check (getParam (p, "s9Slide") != sl, "clic sur SLIDE");
    setParam (p, "s2Octave", 1.0f);
    click (centre (1, StepGrid::upRow));
    check (getParam (p, "s2Octave") == 2.0f, "clic sur UP");
    click (centre (1, StepGrid::downRow));
    check (getParam (p, "s2Octave") == 0.0f, "clic sur DOWN");
    setParam (p, "s6Time", 0.0f);
    click (centre (5, StepGrid::timeRow));
    check (getParam (p, "s6Time") == 1.0f, "NOTE -> TIE");
    click (centre (5, StepGrid::timeRow));
    check (getParam (p, "s6Time") == 2.0f, "TIE -> REST");

    // Longueur par clic sur un numéro
    click (centre (11, StepGrid::numberRow));
    check (getParam (p, Proc::lengthId) == 12.0f, "clic sur le numero 12 : longueur " + juce::String (getParam (p, Proc::lengthId)));

    // Tirer une note vers le haut
    setParam (p, "s10Pitch", 2.0f);
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto start = centre (9, StepGrid::noteRow);
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent down (source, start, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &grid, &grid, now, start, now, 1, false);
        grid.mouseDown (down);
        const auto to = start.translated (0.0f, -40.0f);
        const juce::MouseEvent drag (source, to, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &grid, &grid, now, start, now, 1, true);
        grid.mouseDrag (drag);
        grid.mouseUp (drag);
    }
    check (getParam (p, "s10Pitch") == 7.0f, "tirer de 40 px : +5 demi-tons (" + juce::String (getParam (p, "s10Pitch")) + ")");

    // Molette
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto at = centre (9, StepGrid::noteRow);
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent e (source, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &grid, &grid, now, at, now, 0, false);
        juce::MouseWheelDetails wheel { 0.0f, 0.2f, false, false, false };
        grid.mouseWheelMove (e, wheel);
    }
    check (getParam (p, "s10Pitch") == 8.0f, "molette : +1 demi-ton");

    // Rendus de l'interface (pour les regarder)
    if (outputDir != juce::File())
    {
        p.presets.loadFactory (12);
        editor->getFacade().repaint();
        for (const auto& [name, scale] : { std::pair<const char*, float> { "ui_100.png", 1.0f }, { "ui_2x.png", 2.0f } })
        {
            juce::PNGImageFormat png;
            juce::FileOutputStream out (outputDir.getChildFile (name));
            if (out.openedOk())
            {
                out.setPosition (0);
                out.truncate();
                png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds(), true, scale), out);
            }
        }
    }

    p.editorBeingDeleted (ed.get());
}

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << "Tests du plugin Acid Machine" << std::endl;

    const auto outputDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outputDir != juce::File())
        outputDir.createDirectory();

    testParameters();
    testFactoryBank();
    testMidiMode();
    testSequencerMode();
    testRobustness();
    testState();
    testEditor (outputDir);
    testPresetsAndWindowSize<Proc, AcidMachineAudioProcessorEditor, AcidMachineFacade> ({}, outputDir, "INIT");

    if (failures == 0)
        std::cout << "OK" << std::endl;
    else
        std::cout << failures << " echec(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
