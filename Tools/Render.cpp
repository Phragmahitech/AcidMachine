// Rendu hors ligne des presets d'usine (pour écouter / mesurer sans DAW).
// Usage : AcidMachineRender <dossier> [bpm] [mesures]
// Un fichier WAV 24 bits par preset, joué par le séquenceur interne avec un hôte simulé.

#include "../Source/PluginProcessor.h"
#include "../Source/FactoryPresets.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

struct RenderPlayHead final : public juce::AudioPlayHead
{
    double bpm = 140.0, ppq = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        return info;
    }
};

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cout << "Usage : AcidMachineRender <dossier> [bpm] [mesures]" << std::endl;
        return 1;
    }

    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();
    const double bpm = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 140.0;
    const int bars = argc > 3 ? juce::String (argv[3]).getIntValue() : 4;
    const double rate = 48000.0;
    const int block = 256;

    const auto& list = acid::factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        auto& p = dynamic_cast<AcidMachineAudioProcessor&> (*base);
        RenderPlayHead head;
        head.bpm = bpm;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, rate, block);
        p.prepareToPlay (rate, block);
        p.presets.loadFactory (i);

        const int total = (int) (bars * 4 * 60.0 / bpm * rate) + (int) (0.3 * rate);
        juce::AudioBuffer<float> all (2, total);
        all.clear();
        float top = 0.0f;

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> buffer (2, n);
            juce::MidiBuffer midi;
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom (ch, pos, buffer, ch, 0, n);
            top = juce::jmax (top, buffer.getMagnitude (0, 0, n));
            head.ppq += bpm / 60.0 * n / rate;
        }

        auto name = juce::String (i).paddedLeft ('0', 2) + "-" + juce::String (list[(size_t) i].name).replace (" - ", "-").replace (" ", "-").toLowerCase();
        auto file = dir.getChildFile (name + ".wav");
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                                                     .withNumChannels (2)
                                                                                     .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (all, 0, total);

        std::cout << name << "  crete " << juce::Decibels::gainToDecibels (top) << " dBFS" << std::endl;
        p.releaseResources();
    }

    return 0;
}
