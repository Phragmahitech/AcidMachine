#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AcidVoice.h"
#include "AcidSequencer.h"
#include "PresetLibrary.h"

class AcidMachineAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets sauvegardés en dépendent)
    static constexpr const char* tuningId     = "tuning";
    static constexpr const char* cutoffId     = "cutoff";
    static constexpr const char* resonanceId  = "resonance";
    static constexpr const char* envModId     = "envMod";
    static constexpr const char* decayId      = "decay";
    static constexpr const char* accentId     = "accent";
    static constexpr const char* waveformId   = "waveform";
    static constexpr const char* volumeId     = "volume";
    static constexpr const char* slideTimeId  = "slideTime";

    static constexpr const char* seqId        = "seq";
    static constexpr const char* lengthId     = "length";
    static constexpr const char* rateId       = "rate";
    static constexpr const char* swingId      = "swing";
    static constexpr const char* transposeId  = "transpose";

    /** Paramètres d'un pas : "s1Pitch", "s1Octave", "s1Time", "s1Accent", "s1Slide"… "s16Slide". */
    static juce::String stepParamId (int step, const char* field)   { return "s" + juce::String (step + 1) + field; }

    static constexpr int numSteps = acid::Pattern::maxSteps;
    static constexpr int accentVelocity = 110;   // en MIDI : vélocité à partir de laquelle la note est accentuée
    static constexpr int transposeCentre = 60;   // en mode SEQ, la note MIDI C3 ne transpose pas

    static const juce::StringArray& rateNames();
    static double rateInBeats (int index);
    static const juce::StringArray& pitchNames();

    //==========================================================================
    AcidMachineAudioProcessor();
    ~AcidMachineAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Acid Machine"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.1; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Presets : la banque d'usine (INIT, MINOR, MAJOR, ACID) + ceux de l'utilisateur (.acidpreset). */
    PresetLibrary presets;

    /** Applique un preset d'usine (tous les paramètres). */
    void applyFactoryPreset (int index);
    /** Index du preset d'usine qui correspond aux réglages actuels, sinon -1. */
    int matchingFactoryPreset() const;

    /** Le motif tel que les paramètres le décrivent. */
    acid::Pattern readPattern() const;

    //==========================================================================
    // Pour l'interface (lecture seule)
    int getPlayingStep() const noexcept                     { return playingStep.load (std::memory_order_relaxed); }
    bool isSequencerRunning() const noexcept                { return sequencerRunning.load (std::memory_order_relaxed); }
    float takeOutputPeak() noexcept                         { return outputPeak.exchange (0.0f, std::memory_order_relaxed); }
    /** Dernière note jouée (-1 au repos) et son accent, pour l'afficheur. */
    int getSoundingNote() const noexcept                    { return soundingNote.load (std::memory_order_relaxed); }
    bool isSoundingAccent() const noexcept                  { return soundingAccent.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct PendingEvent
    {
        int offset;
        int order;
        acid::AcidSequencer::EventType type;
        int note;
        bool accent;
        bool legatoBack;   // MIDI : retour sur une note encore tenue
    };

    void handleMidi (const juce::MidiMessage&, int offset, bool sequencerMode, std::vector<PendingEvent>&);
    void applyEvent (const PendingEvent&);
    void updateVoiceSettings (bool instant);

    acid::AcidVoice voice;
    acid::AcidSequencer sequencer;

    // Paramètres
    std::atomic<float>* tuningParam = nullptr;
    std::atomic<float>* cutoffParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* envModParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* accentParam = nullptr;
    std::atomic<float>* waveformParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* slideTimeParam = nullptr;
    std::atomic<float>* seqParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* swingParam = nullptr;
    std::atomic<float>* transposeParam = nullptr;

    struct StepParams
    {
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* time = nullptr;
        std::atomic<float>* accent = nullptr;
        std::atomic<float>* slide = nullptr;
    };
    std::array<StepParams, numSteps> stepParams;

    // Lissage des potards (sinon un balayage automatisé avance par marches)
    struct Smoothed { double value = 0.5; };
    Smoothed smoothCutoff, smoothResonance, smoothEnvMod, smoothDecay, smoothAccent, smoothTuning;
    double smoothingCoeff = 0.0;
    double volumeGain = 1.0, volumeStep = 0.0;

    // MIDI
    std::vector<int> heldNotes;
    std::vector<PendingEvent> events;
    int midiTranspose = 0;
    bool lastSequencerMode = true;

    double currentSampleRate = 44100.0;

    std::atomic<int> playingStep { -1 };
    std::atomic<bool> sequencerRunning { false };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int> soundingNote { -1 };
    std::atomic<bool> soundingAccent { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcidMachineAudioProcessor)
};
