#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "PatternGenerator.h"

//==============================================================================
const juce::StringArray& AcidMachineAudioProcessor::rateNames()
{
    static const juce::StringArray names { "1/8", "1/16", "1/16T", "1/32" };
    return names;
}

double AcidMachineAudioProcessor::rateInBeats (int index)
{
    switch (index)
    {
        case 0:  return 0.5;
        case 2:  return 1.0 / 6.0;
        case 3:  return 0.125;
        default: return 0.25;
    }
}

const juce::StringArray& AcidMachineAudioProcessor::pitchNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'" };
    return names;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AcidMachineAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto knob = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.01f), def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (percent).withLabel ("%"));
    };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tuningId, 1 }, "Tuning", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                    {
                        return (v > 0.0f ? "+" : "") + String (v, 2) + " st";
                    })));
    layout.add (knob (cutoffId,    "Cut Off Freq", 50.0f));
    layout.add (knob (resonanceId, "Resonance",    50.0f));
    layout.add (knob (envModId,    "Env Mod",      50.0f));
    layout.add (knob (decayId,     "Decay",        50.0f));
    layout.add (knob (accentId,    "Accent",       50.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { waveformId, 1 }, "Waveform", StringArray { "SAW", "SQUARE" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { volumeId, 1 }, "Volume", NormalisableRange<float> (-36.0f, 6.0f, 0.01f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                    {
                        return (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
                    })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { slideTimeId, 1 }, "Slide Time", NormalisableRange<float> (10.0f, 300.0f, 0.01f, 0.5f), 60.0f,
                    AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
                    {
                        return String (juce::roundToInt (v)) + " ms";
                    })));

    layout.add (knob (driveId, "Drive", 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { driveModeId, 1 }, "Drive Mode", StringArray { "SOFT", "DIODE 1", "DIODE 2" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { seqTriggerId, 1 }, "Sequencer Trigger", StringArray { "HOST", "MIDI NOTE" }, 1));

    StringArray styles;
    for (int i = 0; i < acid::PatternGenerator::numStyles; ++i)
        styles.add (acid::PatternGenerator::styleName (i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { randomStyleId, 1 }, "Random Style", styles, (int) acid::PatternGenerator::acid));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { seqId, 1 }, "Sequencer", true));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { lengthId, 1 }, "Length", 1, numSteps, numSteps));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { rateId, 1 }, "Rate", rateNames(), 1));
    layout.add (knob (swingId, "Swing", 0.0f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { transposeId, 1 }, "Transpose", -12, 12, 0, AudioParameterIntAttributes()
                    .withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));

    for (int i = 0; i < numSteps; ++i)
    {
        const auto n = String (i + 1);
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { stepParamId (i, "Pitch"), 1 }, "Step " + n + " Note", 0, 12, 0,
                        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return pitchNames()[juce::jlimit (0, 12, v)]; })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { stepParamId (i, "Octave"), 1 }, "Step " + n + " Octave",
                                                            StringArray { "DOWN", "MID", "UP" }, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { stepParamId (i, "Time"), 1 }, "Step " + n + " Time",
                                                            StringArray { "NOTE", "TIE", "REST" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { stepParamId (i, "Accent"), 1 }, "Step " + n + " Accent", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { stepParamId (i, "Slide"), 1 }, "Step " + n + " Slide", false));
    }

    return layout;
}

//==============================================================================
AcidMachineAudioProcessor::AcidMachineAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AcidMachine", createParameterLayout()),
      presets (apvts, "AcidMachine", ".acidpreset",
               PresetLibrary::Factory {
                   [] { return (int) acid::factoryPresets().size(); },
                   [] (int i) { return juce::String (acid::factoryPresets()[(size_t) i].name); },
                   [this] (int i) { applyFactoryPreset (i); },
                   [this] { return matchingFactoryPreset(); } },
               { randomStyleId, seqTriggerId }, "UNTITLED")
{
    tuningParam    = apvts.getRawParameterValue (tuningId);
    cutoffParam    = apvts.getRawParameterValue (cutoffId);
    resonanceParam = apvts.getRawParameterValue (resonanceId);
    envModParam    = apvts.getRawParameterValue (envModId);
    decayParam     = apvts.getRawParameterValue (decayId);
    accentParam    = apvts.getRawParameterValue (accentId);
    waveformParam  = apvts.getRawParameterValue (waveformId);
    volumeParam    = apvts.getRawParameterValue (volumeId);
    slideTimeParam = apvts.getRawParameterValue (slideTimeId);
    driveParam     = apvts.getRawParameterValue (driveId);
    driveModeParam = apvts.getRawParameterValue (driveModeId);
    seqTriggerParam = apvts.getRawParameterValue (seqTriggerId);
    seqParam       = apvts.getRawParameterValue (seqId);
    lengthParam    = apvts.getRawParameterValue (lengthId);
    rateParam      = apvts.getRawParameterValue (rateId);
    swingParam     = apvts.getRawParameterValue (swingId);
    transposeParam = apvts.getRawParameterValue (transposeId);

    for (int i = 0; i < numSteps; ++i)
    {
        auto& s = stepParams[(size_t) i];
        s.pitch  = apvts.getRawParameterValue (stepParamId (i, "Pitch"));
        s.octave = apvts.getRawParameterValue (stepParamId (i, "Octave"));
        s.time   = apvts.getRawParameterValue (stepParamId (i, "Time"));
        s.accent = apvts.getRawParameterValue (stepParamId (i, "Accent"));
        s.slide  = apvts.getRawParameterValue (stepParamId (i, "Slide"));
    }

    heldNotes.reserve (128);
    events.reserve (512);

    // Plugin neuf : réglages = INIT, reconnu comme tel
    presets.restoreFromState();
}

//==============================================================================
namespace
{
    /** Valeur (en unités réelles) de chaque paramètre pour un preset d'usine. */
    std::vector<std::pair<juce::String, float>> factoryValues (const acid::FactoryPreset& p)
    {
        using P = AcidMachineAudioProcessor;
        std::vector<std::pair<juce::String, float>> v;

        v.push_back ({ P::tuningId, 0.0f });
        v.push_back ({ P::cutoffId, p.cutoff });
        v.push_back ({ P::resonanceId, p.resonance });
        v.push_back ({ P::envModId, p.envMod });
        v.push_back ({ P::decayId, p.decay });
        v.push_back ({ P::accentId, p.accent });
        v.push_back ({ P::waveformId, p.square ? 1.0f : 0.0f });
        v.push_back ({ P::volumeId, 0.0f });
        v.push_back ({ P::slideTimeId, 60.0f });
        v.push_back ({ P::driveId, p.drive });
        v.push_back ({ P::driveModeId, (float) p.driveMode });
        v.push_back ({ P::seqId, 1.0f });
        v.push_back ({ P::lengthId, (float) p.length });
        v.push_back ({ P::rateId, (float) p.rate });
        v.push_back ({ P::swingId, p.swing });
        v.push_back ({ P::transposeId, 0.0f });

        const auto pattern = acid::parsePattern (p);
        for (int i = 0; i < P::numSteps; ++i)
        {
            const auto& s = pattern.steps[(size_t) i];
            v.push_back ({ P::stepParamId (i, "Pitch"), (float) s.pitch });
            v.push_back ({ P::stepParamId (i, "Octave"), (float) s.octave });
            v.push_back ({ P::stepParamId (i, "Time"), (float) s.time });
            v.push_back ({ P::stepParamId (i, "Accent"), s.accent ? 1.0f : 0.0f });
            v.push_back ({ P::stepParamId (i, "Slide"), s.slide ? 1.0f : 0.0f });
        }

        return v;
    }
}

void AcidMachineAudioProcessor::applyFactoryPreset (int index)
{
    const auto& list = acid::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;

    for (const auto& [id, value] : factoryValues (list[(size_t) index]))
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
}

int AcidMachineAudioProcessor::matchingFactoryPreset() const
{
    const auto& list = acid::factoryPresets();

    for (size_t i = 0; i < list.size(); ++i)
    {
        bool same = true;
        for (const auto& [id, value] : factoryValues (list[i]))
            if (auto* p = apvts.getParameter (id); p == nullptr || std::abs (p->getValue() - p->convertTo0to1 (value)) > 1.0e-4f)
            {
                same = false;
                break;
            }

        if (same)
            return (int) i;
    }

    return -1;
}

acid::Pattern AcidMachineAudioProcessor::readPattern() const
{
    acid::Pattern p;
    p.length = juce::jlimit (1, numSteps, (int) std::lround (lengthParam->load()));

    for (int i = 0; i < numSteps; ++i)
    {
        const auto& sp = stepParams[(size_t) i];
        auto& s = p.steps[(size_t) i];
        s.pitch  = juce::jlimit (0, 12, (int) std::lround (sp.pitch->load()));
        s.octave = juce::jlimit (0, 2, (int) std::lround (sp.octave->load()));
        s.time   = juce::jlimit (0, 2, (int) std::lround (sp.time->load()));
        s.accent = sp.accent->load() > 0.5f;
        s.slide  = sp.slide->load() > 0.5f;
    }

    return p;
}

void AcidMachineAudioProcessor::writePattern (const acid::Pattern& pattern)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    set (lengthId, (float) pattern.length);
    for (int i = 0; i < numSteps; ++i)
    {
        const auto& s = pattern.steps[(size_t) i];
        set (stepParamId (i, "Pitch"), (float) s.pitch);
        set (stepParamId (i, "Octave"), (float) s.octave);
        set (stepParamId (i, "Time"), (float) s.time);
        set (stepParamId (i, "Accent"), s.accent ? 1.0f : 0.0f);
        set (stepParamId (i, "Slide"), s.slide ? 1.0f : 0.0f);
    }
}

void AcidMachineAudioProcessor::randomizePattern (uint32_t seed)
{
    if (seed == 0)
        seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;

    const int style = juce::roundToInt (apvts.getRawParameterValue (randomStyleId)->load());
    writePattern (acid::PatternGenerator::generate (style, seed));
}

//==============================================================================
void AcidMachineAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    voice.prepare (currentSampleRate);

    // Lissage des potards, recalculé tous les 32 échantillons (constante de temps 15 ms)
    smoothingCoeff = std::exp (-32.0 / (0.015 * currentSampleRate));
    updateVoiceSettings (true);
    volumeGain = juce::Decibels::decibelsToGain ((double) volumeParam->load(), -100.0);

    heldNotes.clear();
    midiTranspose = 0;
    noteSequenceRunning = false;
    sequencer.stop ([] (int, const acid::AcidSequencer::Event&) {});
    playingStep = -1;
    sequencerRunning = false;
    soundingNote = -1;
}

void AcidMachineAudioProcessor::reset()
{
    voice.kill();
    heldNotes.clear();
    midiTranspose = 0;
    noteSequenceRunning = false;
    sequencer.stop ([] (int, const acid::AcidSequencer::Event&) {});
    playingStep = -1;
    sequencerRunning = false;
    soundingNote = -1;
}

bool AcidMachineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void AcidMachineAudioProcessor::updateVoiceSettings (bool instant)
{
    auto follow = [this, instant] (Smoothed& s, double target)
    {
        s.value = instant ? target : target + (s.value - target) * smoothingCoeff;
    };

    follow (smoothCutoff,    cutoffParam->load() * 0.01);
    follow (smoothResonance, resonanceParam->load() * 0.01);
    follow (smoothEnvMod,    envModParam->load() * 0.01);
    follow (smoothDecay,     decayParam->load() * 0.01);
    follow (smoothAccent,    accentParam->load() * 0.01);
    follow (smoothTuning,    tuningParam->load());
    follow (smoothDrive,     driveParam->load() * 0.01);

    acid::VoiceSettings s;
    s.tuning    = smoothTuning.value;
    s.cutoff    = smoothCutoff.value;
    s.resonance = smoothResonance.value;
    s.envMod    = smoothEnvMod.value;
    s.decay     = smoothDecay.value;
    s.accent    = smoothAccent.value;
    s.square    = waveformParam->load() > 0.5f;
    s.slideMs   = slideTimeParam->load();
    s.drive     = smoothDrive.value < 1.0e-4 ? 0.0 : smoothDrive.value;
    s.driveMode = (int) std::lround (driveModeParam->load());
    voice.updateSettings (s);
}

//==============================================================================
void AcidMachineAudioProcessor::handleMidi (const juce::MidiMessage& m, int offset, bool sequencerMode, std::vector<PendingEvent>& out)
{
    using Type = acid::AcidSequencer::EventType;
    const int order = (int) out.size();

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        const bool accented = m.getVelocity() >= accentVelocity;

        if (sequencerMode)
        {
            midiTranspose = note - transposeCentre;
        }
        else
        {
            out.push_back ({ offset, order, heldNotes.empty() ? Type::trigger : Type::slide, note, accented, false });
        }

        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        const bool wasTop = ! heldNotes.empty() && heldNotes.back() == note;
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

        if (sequencerMode)
        {
            midiTranspose = heldNotes.empty() ? 0 : heldNotes.back() - transposeCentre;
        }
        else if (heldNotes.empty())
        {
            out.push_back ({ offset, order, Type::release, note, false, false });
        }
        else if (wasTop)
        {
            // Retour sur la note encore tenue : glissé, sans nouvelle attaque (comme Open303)
            out.push_back ({ offset, order, Type::slide, heldNotes.back(), false, true });
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clear();
        midiTranspose = 0;
        out.push_back ({ offset, order, Type::release, 0, false, false });
    }
}

void AcidMachineAudioProcessor::applyEvent (const PendingEvent& e)
{
    using Type = acid::AcidSequencer::EventType;

    switch (e.type)
    {
        case Type::trigger:
            voice.noteOn (e.note, e.accent, false);
            soundingNote = e.note;
            soundingAccent = e.accent;
            break;

        case Type::slide:
            if (e.legatoBack)
                voice.glideTo (e.note);
            else
                voice.noteOn (e.note, e.accent, true);
            soundingNote = e.note;
            if (! e.legatoBack)
                soundingAccent = e.accent;
            break;

        case Type::release:
            voice.noteOff();
            soundingNote = -1;
            break;
    }
}

void AcidMachineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0)
        return;

    const bool sequencerMode = seqParam->load() > 0.5f;
    const int triggerMode = (int) std::lround (seqTriggerParam->load());
    if (sequencerMode != lastSequencerMode || triggerMode != lastTriggerMode)
    {
        noteSequenceRunning = false;
        lastTriggerMode = triggerMode;
        voice.noteOff();
        heldNotes.clear();
        midiTranspose = 0;
        sequencer.stop ([] (int, const acid::AcidSequencer::Event&) {});
        soundingNote = -1;
        lastSequencerMode = sequencerMode;
    }

    events.clear();

    if (sequencerMode && triggerMode == 1)
    {
        // MIDI NOTE : la séquence part du pas 1 à chaque note et s'arrête quand on la lâche
        double bpm = 120.0;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                if (const auto b = position->getBpm())
                    bpm = *b;

        runNoteTriggeredSequencer (midi, numSamples, bpm);
        playingStep = noteSequenceRunning ? sequencer.getCurrentStep() : -1;
        sequencerRunning = noteSequenceRunning;
    }
    else
    {
    for (const auto metadata : midi)
        handleMidi (metadata.getMessage(), juce::jlimit (0, numSamples - 1, metadata.samplePosition), sequencerMode, events);

    if (sequencerMode)
    {
        sequencer.setPattern (readPattern());
        sequencer.setStepBeats (rateInBeats ((int) std::lround (rateParam->load())));
        sequencer.setSwing (swingParam->load() * 0.01);
        sequencer.setTranspose ((int) std::lround (transposeParam->load()) + midiTranspose);

        bool playing = false;
        double ppq = 0.0, bpm = 120.0;

        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                playing = position->getIsPlaying();
                if (const auto p = position->getPpqPosition()) ppq = *p; else playing = false;
                if (const auto b = position->getBpm()) bpm = *b;
            }

        sequencer.process (playing, ppq, bpm, currentSampleRate, numSamples,
                           [this] (int offset, const acid::AcidSequencer::Event& e)
                           {
                               events.push_back ({ offset, (int) events.size(), e.type, e.note, e.accent, false });
                           });

        playingStep = sequencer.getCurrentStep();
        sequencerRunning = playing;
    }
    else
    {
        playingStep = -1;
        sequencerRunning = false;
    }
    }

    std::stable_sort (events.begin(), events.end(), [] (const PendingEvent& a, const PendingEvent& b)
    {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });

    // Rendu, coupé aux événements et tous les 32 échantillons (lissage des potards)
    auto* left = buffer.getWritePointer (0);
    const double volumeTarget = juce::Decibels::decibelsToGain ((double) volumeParam->load(), -100.0);
    float peak = 0.0f;
    size_t nextEvent = 0;
    int position = 0;

    while (position < numSamples)
    {
        while (nextEvent < events.size() && events[nextEvent].offset <= position)
            applyEvent (events[nextEvent++]);

        int end = juce::jmin (numSamples, position + 32);
        if (nextEvent < events.size())
            end = juce::jmin (end, events[nextEvent].offset);

        updateVoiceSettings (false);
        const double gainStep = (volumeTarget - volumeGain) * 0.02;

        for (int i = position; i < end; ++i)
        {
            volumeGain += gainStep;
            const auto y = (float) (voice.process() * volumeGain);
            left[i] = y;
            peak = juce::jmax (peak, std::abs (y));
        }

        if (std::abs (volumeTarget - volumeGain) < 1.0e-6)
            volumeGain = volumeTarget;

        position = end;
    }

    while (nextEvent < events.size())
        applyEvent (events[nextEvent++]);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    if (peak > outputPeak.load (std::memory_order_relaxed))
        outputPeak.store (peak, std::memory_order_relaxed);
}

void AcidMachineAudioProcessor::runNoteTriggeredSequencer (const juce::MidiBuffer& midi, int numSamples, double bpm)
{
    sequencer.setPattern (readPattern());
    sequencer.setStepBeats (rateInBeats ((int) std::lround (rateParam->load())));
    sequencer.setSwing (swingParam->load() * 0.01);

    const double beatsPerSample = bpm / (60.0 * currentSampleRate);
    const int baseTranspose = (int) std::lround (transposeParam->load());

    auto emitAt = [this] (int base)
    {
        return [this, base] (int offset, const acid::AcidSequencer::Event& e)
        {
            events.push_back ({ base + offset, (int) events.size(), e.type, e.note, e.accent, false });
        };
    };

    // Avance la séquence de position jusqu'à end (exclu)
    int position = 0;
    auto advanceTo = [&] (int end)
    {
        if (end <= position)
            return;
        if (noteSequenceRunning)
        {
            sequencer.setTranspose (baseTranspose + midiTranspose);
            sequencer.process (true, notePpq, bpm, currentSampleRate, end - position, emitAt (position));
            notePpq += beatsPerSample * (end - position);
        }
        position = end;
    };

    for (const auto metadata : midi)
    {
        const auto& m = metadata.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
        advanceTo (offset);

        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            const bool first = heldNotes.empty();
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            heldNotes.push_back (note);
            midiTranspose = note - transposeCentre;

            if (first)
            {
                // Nouvelle note (rien n'était tenu) : départ au pas 1, ici
                sequencer.stop (emitAt (offset));
                noteSequenceRunning = true;
                notePpq = 0.0;
            }
        }
        else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())
        {
            if (m.isNoteOff())
                heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
            else
                heldNotes.clear();

            if (heldNotes.empty())
            {
                sequencer.stop (emitAt (offset));
                noteSequenceRunning = false;
                midiTranspose = 0;
            }
            else
            {
                midiTranspose = heldNotes.back() - transposeCentre;   // legato : on change seulement de tonalité
            }
        }
    }

    advanceTo (numSamples);
}

void AcidMachineAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    reset();
}

//==============================================================================
juce::AudioProcessorEditor* AcidMachineAudioProcessor::createEditor()
{
    return new AcidMachineAudioProcessorEditor (*this);
}

void AcidMachineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AcidMachineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        presets.restoreFromState();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AcidMachineAudioProcessor();
}
