#pragma once

#include "PluginProcessor.h"
#include "PresetBar.h"

/**
    Façade d'Acid Machine : plaque argent brossé, bandeau noir avec le smiley acid,
    potards noirs à jupe, séquenceur 16 pas sur un panneau graphite.
    Police Chakra Petch embarquée (licence OFL).
*/
class AcidLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AcidLookAndFeel();

    // Palette
    static juce::Colour ink()        { return juce::Colour (0xff141414); }
    static juce::Colour dimInk()     { return juce::Colour (0xff55585d); }
    static juce::Colour acid()       { return juce::Colour (0xffffd000); }
    static juce::Colour acidDeep()   { return juce::Colour (0xffffa800); }
    static juce::Colour hot()        { return juce::Colour (0xffff5a1f); }
    static juce::Colour graphite()   { return juce::Colour (0xff222326); }
    static juce::Colour lcd()        { return juce::Colour (0xff141517); }

    juce::Font font (float pointHeight, bool bold, float tracking = 0.0f) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Le smiley acid (dessin maison) : cercle jaune, yeux, sourire, une goutte qui coule. */
    static void drawSmiley (juce::Graphics&, juce::Rectangle<float> area);

private:
    juce::Typeface::Ptr semiBoldFace, boldFace;
};

//==============================================================================
/** Les 16 pas : note (tirer / molette / clic droit), octave UP / DOWN, NOTE / TIE / REST, ACCENT, SLIDE. */
class StepGrid final : public juce::Component
{
public:
    StepGrid (AcidMachineAudioProcessor&, AcidLookAndFeel&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    enum Row { numberRow, noteRow, upRow, downRow, timeRow, accentRow, slideRow, numRows };

    /** Zone d'une case (coordonnées du composant). */
    juce::Rectangle<int> cellBounds (int column, Row row) const;
    /** Case sous un point : false si aucune. */
    bool cellAt (juce::Point<int>, int& column, Row& row) const;

    static constexpr int firstColumnX = 66, columnWidth = 62;

private:
    struct StepParameters
    {
        juce::RangedAudioParameter* pitch = nullptr;
        juce::RangedAudioParameter* octave = nullptr;
        juce::RangedAudioParameter* time = nullptr;
        juce::RangedAudioParameter* accent = nullptr;
        juce::RangedAudioParameter* slide = nullptr;
    };

    static int valueOf (const juce::RangedAudioParameter* p)
    {
        return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    }

    static void setValue (juce::RangedAudioParameter* p, int value, bool gesture = true)
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
        if (gesture) p->endChangeGesture();
    }

    void showNoteMenu (int column);

    AcidMachineAudioProcessor& processor;
    AcidLookAndFeel& lf;
    std::array<StepParameters, AcidMachineAudioProcessor::numSteps> steps;
    juce::RangedAudioParameter* lengthParameter = nullptr;

    // Tirer sur une note / peindre une rangée d'interrupteurs
    int dragColumn = -1, dragStartPitch = 0;
    Row dragRow = numberRow;
    int paintValue = 0;
    juce::RangedAudioParameter* draggedParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

//==============================================================================
/** La façade à taille fixe, mise à l'échelle par l'éditeur. */
class AcidMachineFacade final : public juce::Component,
                                private juce::Timer
{
public:
    static constexpr int width = 1100, height = 720;

    explicit AcidMachineFacade (AcidMachineAudioProcessor&);
    ~AcidMachineFacade() override;

    phragma::PresetBar& getPresetBar() noexcept     { return presetBar; }
    StepGrid& getStepGrid() noexcept                { return stepGrid; }

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct ChoiceSwitch
    {
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    void addKnob (juce::Slider&, const char* parameterId, juce::Rectangle<int> bounds, const char* size);
    void addSwitch (ChoiceSwitch&, const char* parameterId, const juce::StringArray& texts, juce::Rectangle<int> area, bool vertical);
    void timerCallback() override;
    void paintBackground (juce::Graphics&);
    void paintDynamic (juce::Graphics&);

    AcidMachineAudioProcessor& audioProcessor;
    AcidLookAndFeel lookAndFeel;

    juce::Image background;
    float backgroundScale = 0.0f;

    juce::Slider tuningKnob, cutoffKnob, resonanceKnob, envModKnob, decayKnob, accentKnob;
    juce::Slider slideKnob, volumeKnob, lengthKnob, swingKnob, transposeKnob;
    juce::TextButton seqButton { "SEQ" };
    ChoiceSwitch waveformSwitch, rateSwitch;
    StepGrid stepGrid;

    struct KnobLabel { juce::Slider* slider; juce::String parameterId; };
    std::vector<KnobLabel> knobLabels;
    bool accentLit = false, noteLit = false;
    int shownStep = -2;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;

    // Barre des presets et de la taille : déclarée en dernier, détruite en premier.
    phragma::PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcidMachineFacade)
};

//==============================================================================
/** L'éditeur : la façade mise à l'échelle de la fenêtre (60 à 150 %, proportions gardées). */
class AcidMachineAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AcidMachineAudioProcessorEditor (AcidMachineAudioProcessor&);
    ~AcidMachineAudioProcessorEditor() override;

    void resized() override;

    AcidMachineFacade& getFacade() noexcept             { return facade; }

    float getUiScale() const                            { return (float) getWidth() / (float) AcidMachineFacade::width; }
    void setUiScale (float scale);

private:
    AcidMachineAudioProcessor& audioProcessor;
    AcidMachineFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcidMachineAudioProcessorEditor)
};
