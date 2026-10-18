#include "PluginEditor.h"
#include "BinaryData.h"
#include "PatternGenerator.h"

namespace
{
    constexpr int W = AcidMachineFacade::width, H = AcidMachineFacade::height;
    constexpr int headerHeight = 64;
    constexpr int voiceTop = headerHeight, voiceBottom = 318;
    constexpr int seqRowTop = voiceBottom, gridTop = 428, gridBottom = 708;

    // Potards de la voix : centres
    constexpr int knobY = 192;
    constexpr int bigKnob = 100, midKnob = 78, miniKnob = 54;
    constexpr int knobX[] = { 92, 212, 332, 452, 572, 692 };
    constexpr int driveX = 912, slideX = 984, volumeX = 1050;
    constexpr int outKnob = 66;

    // Rangée du séquenceur
    constexpr int seqControlsY = 376;
    constexpr int lengthX = 470, swingX = 548, transposeX = 626;

    juce::Rectangle<int> knobBounds (int cx, int cy, int size)
    {
        return { cx - size / 2, cy - size / 2, size, size };
    }

    juce::Path roundedBox (juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        return p;
    }
}

//==============================================================================
AcidLookAndFeel::AcidLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf,
                                                            (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf,
                                                        (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff4f4f2));
    setColour (juce::PopupMenu::textColourId, ink());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, acid());
    setColour (juce::PopupMenu::highlightedTextColourId, ink());
}

juce::Font AcidLookAndFeel::font (float pointHeight, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (pointHeight));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

void AcidLookAndFeel::drawSmiley (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto d = juce::jmin (area.getWidth(), area.getHeight());
    const auto face = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
    const float r = d * 0.5f;
    const auto c = face.getCentre();
    const float line = juce::jmax (1.2f, d * 0.055f);

    // Goutte qui coule du bas à droite
    juce::Path drip;
    const float dx = c.x + r * 0.42f, dy = c.y + r * 0.78f;
    drip.startNewSubPath (dx - r * 0.14f, dy - r * 0.10f);
    drip.cubicTo (dx - r * 0.12f, dy + r * 0.25f, dx - r * 0.13f, dy + r * 0.42f, dx, dy + r * 0.52f);
    drip.cubicTo (dx + r * 0.13f, dy + r * 0.42f, dx + r * 0.12f, dy + r * 0.25f, dx + r * 0.14f, dy - r * 0.18f);
    drip.closeSubPath();

    juce::Path disc;
    disc.addEllipse (face);

    juce::Path body (disc);
    body.addPath (drip);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe44d), c.x - r * 0.4f, c.y - r * 0.6f,
                                             acidDeep(), c.x + r * 0.5f, c.y + r * 0.9f, true));
    g.fillPath (body);
    g.setColour (ink());
    g.strokePath (disc, juce::PathStrokeType (line));
    g.strokePath (drip, juce::PathStrokeType (line));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe44d), c.x - r * 0.4f, c.y - r * 0.6f,
                                             acidDeep(), c.x + r * 0.5f, c.y + r * 0.9f, true));
    g.fillEllipse (face.reduced (line * 0.5f));

    // Yeux : deux ovales
    g.setColour (ink());
    const float ew = r * 0.17f, eh = r * 0.36f;
    g.fillEllipse (c.x - r * 0.33f - ew * 0.5f, c.y - r * 0.38f, ew, eh);
    g.fillEllipse (c.x + r * 0.33f - ew * 0.5f, c.y - r * 0.38f, ew, eh);

    // Sourire
    juce::Path smile;
    smile.addCentredArc (c.x, c.y + r * 0.02f, r * 0.58f, r * 0.52f, 0.0f,
                         juce::MathConstants<float>::pi * 0.62f, juce::MathConstants<float>::pi * 1.38f, true);
    g.strokePath (smile, juce::PathStrokeType (line * 1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AcidLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto c = bounds.getCentre();
    const bool mini = slider.getProperties()["size"].toString() == "mini";
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - (mini ? 6.0f : 9.0f);
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Jupe noire crantée
    const int ridges = mini ? 18 : 30;
    juce::Path skirt;
    for (int i = 0; i <= ridges * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (ridges * 2);
        const float rr = outer * (i % 2 == 0 ? 1.0f : 0.965f);
        const auto p = c.getPointOnCircumference (rr, a);
        if (i == 0) skirt.startNewSubPath (p); else skirt.lineTo (p);
    }
    skirt.closeSubPath();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3b3e), c.x, c.y - outer,
                                             juce::Colour (0xff050505), c.x, c.y + outer, false));
    g.fillPath (skirt);

    // Chapeau
    const float capR = outer * 0.70f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4b4f), c.x - capR * 0.4f, c.y - capR * 0.8f,
                                             juce::Colour (0xff111113), c.x + capR * 0.3f, c.y + capR, true));
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c).reduced (0.6f), 1.0f);

    // Trait blanc (jaune quand on tient le potard)
    const bool active = slider.isMouseButtonDown() || slider.isMouseOverOrDragging();
    juce::Path pointer;
    const float w = mini ? 2.4f : 3.2f;
    pointer.addRoundedRectangle (-w * 0.5f, -outer * 0.97f, w, outer * 0.52f, w * 0.5f);
    g.setColour (active ? acid() : juce::Colour (0xfff2f2f0));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void AcidLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto id = b.getComponentID();
    if (id.startsWith ("preset") || id == "windowSize")
        return LookAndFeel_V4::drawButtonBackground (g, b, {}, over, down);

    const auto area = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState() || id == "random";

    // Touche en plastique, légèrement bombée ; allumée = jaune acide
    const auto shape = roundedBox (area, 3.0f);
    if (on)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffe14a), 0.0f, area.getY(), acidDeep(), 0.0f, area.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3f3f1), 0.0f, area.getY(), juce::Colour (0xffcfd0d2), 0.0f, area.getBottom(), false));
    g.fillPath (shape);

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillPath (shape);
    }

    g.setColour (on ? juce::Colour (0xff8a6a00) : (over ? ink() : juce::Colour (0xff7d8086)));
    g.strokePath (shape, juce::PathStrokeType (1.1f));
}

void AcidLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto id = b.getComponentID();
    if (id.startsWith ("preset") || id == "windowSize")
        return LookAndFeel_V4::drawButtonText (g, b, over, down);

    g.setColour (ink());
    g.setFont (font (11.5f, true, 0.12f));
    auto area = b.getLocalBounds().toFloat();

    if (id.startsWith ("seqTrigger"))
    {
        g.setFont (font (9.0f, true, 0.12f));
        g.drawText (b.getButtonText(), area, juce::Justification::centred, false);
        return;
    }

    if (id == "driveMode")
    {
        g.setFont (font (9.5f, true, 0.1f));
        g.drawText (b.getButtonText(), area.withTrimmedRight (12.0f), juce::Justification::centred, false);
        juce::Path caret;
        const float x = area.getRight() - 14.0f, y = area.getCentreY() - 1.5f;
        caret.addTriangle (x, y, x + 6.0f, y, x + 3.0f, y + 4.0f);
        g.setColour (dimInk());
        g.fillPath (caret);
        return;
    }

    if (id == "randomStyle")
    {
        g.drawText (b.getButtonText(), area.reduced (12.0f, 0.0f).withTrimmedRight (14.0f), juce::Justification::centredLeft, true);
        juce::Path caret;
        const float x = area.getRight() - 20.0f, y = area.getCentreY() - 2.0f;
        caret.addTriangle (x, y, x + 8.0f, y, x + 4.0f, y + 5.0f);
        g.setColour (over ? ink() : dimInk());
        g.fillPath (caret);
        return;
    }

    if (id == "random")
    {
        // Dé
        const auto die = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ area.getX() + 26.0f, area.getCentreY() });
        g.setColour (ink());
        g.drawRoundedRectangle (die, 4.0f, 1.6f);
        for (auto [px, py] : { std::pair<float, float> { 0.28f, 0.28f }, { 0.72f, 0.72f }, { 0.5f, 0.5f }, { 0.72f, 0.28f }, { 0.28f, 0.72f } })
            g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre ({ die.getX() + px * die.getWidth(), die.getY() + py * die.getHeight() }));
        g.drawText (b.getButtonText(), area.withTrimmedLeft (34.0f), juce::Justification::centred, false);
        return;
    }

    g.drawText (b.getButtonText(), area, juce::Justification::centred, false);
}

//==============================================================================
StepGrid::StepGrid (AcidMachineAudioProcessor& p, AcidLookAndFeel& l) : processor (p), lf (l)
{
    for (int i = 0; i < AcidMachineAudioProcessor::numSteps; ++i)
    {
        auto& s = steps[(size_t) i];
        s.pitch  = p.apvts.getParameter (AcidMachineAudioProcessor::stepParamId (i, "Pitch"));
        s.octave = p.apvts.getParameter (AcidMachineAudioProcessor::stepParamId (i, "Octave"));
        s.time   = p.apvts.getParameter (AcidMachineAudioProcessor::stepParamId (i, "Time"));
        s.accent = p.apvts.getParameter (AcidMachineAudioProcessor::stepParamId (i, "Accent"));
        s.slide  = p.apvts.getParameter (AcidMachineAudioProcessor::stepParamId (i, "Slide"));
    }

    lengthParameter = p.apvts.getParameter (AcidMachineAudioProcessor::lengthId);
    setRepaintsOnMouseActivity (false);
    setTitle ("Step sequencer");
}

juce::Rectangle<int> StepGrid::cellBounds (int column, Row row) const
{
    const int x = firstColumnX + column * columnWidth + 3;
    const int w = columnWidth - 6;
    switch (row)
    {
        case numberRow: return { x, 8, w, 20 };
        case noteRow:   return { x, 32, w, 100 };
        case upRow:     return { x, 138, w, 22 };
        case downRow:   return { x, 164, w, 22 };
        case timeRow:   return { x, 192, w, 22 };
        case accentRow: return { x, 220, w, 22 };
        case slideRow:  return { x, 248, w, 22 };
        case numRows:
        default:        return {};
    }
}

bool StepGrid::cellAt (juce::Point<int> p, int& column, Row& row) const
{
    if (p.x < firstColumnX || p.x >= firstColumnX + columnWidth * AcidMachineAudioProcessor::numSteps)
        return false;

    column = (p.x - firstColumnX) / columnWidth;
    for (int r = 0; r < numRows; ++r)
        if (cellBounds (column, (Row) r).expanded (3, 2).contains (p))
        {
            row = (Row) r;
            return true;
        }

    return false;
}

void StepGrid::paint (juce::Graphics& g)
{
    using L = AcidLookAndFeel;
    const auto area = getLocalBounds().toFloat();

    // Panneau graphite
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2d31), 0.0f, 0.0f, juce::Colour (0xff1b1c1e), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    // Noms des rangées
    const char* names[] = { "STEP", "NOTE", "UP", "DOWN", "TIME", "ACCENT", "SLIDE" };
    g.setFont (lf.font (9.5f, true, 0.16f));
    for (int r = 0; r < numRows; ++r)
    {
        auto b = cellBounds (0, (Row) r).withX (10).withWidth (firstColumnX - 16);
        g.setColour (juce::Colour (0xffa9abb0));
        g.drawText (names[r], b, juce::Justification::centredLeft, false);
    }

    const int length = juce::jlimit (1, 16, valueOf (lengthParameter));
    const int playing = processor.isSequencerRunning() ? processor.getPlayingStep() : -1;
    const auto& pitchNames = AcidMachineAudioProcessor::pitchNames();

    // Hauteurs (pour le dessin des slides)
    auto noteLevel = [this] (int i)
    {
        const auto& s = steps[(size_t) i];
        return (float) (valueOf (s.octave) * 12 + valueOf (s.pitch)) / 36.0f;
    };
    auto barTop = [this, &noteLevel] (int i)
    {
        const auto b = cellBounds (i, noteRow).toFloat().reduced (4.0f, 6.0f).withTrimmedBottom (18.0f);
        return b.getBottom() - noteLevel (i) * b.getHeight();
    };

    // Note « héritée » par un TIE (la dernière NOTE avant lui)
    int lastNoteStep = -1;

    for (int i = 0; i < AcidMachineAudioProcessor::numSteps; ++i)
    {
        const auto& s = steps[(size_t) i];
        const bool inPattern = i < length;
        const float alpha = inPattern ? 1.0f : 0.28f;
        const int time = valueOf (s.time);
        const bool accent = valueOf (s.accent) != 0, slide = valueOf (s.slide) != 0;
        const int octave = valueOf (s.octave), pitch = valueOf (s.pitch);

        // Numéro + voyant
        {
            auto b = cellBounds (i, numberRow).toFloat();
            const bool lit = i == playing;
            g.setColour ((lit ? L::acid() : juce::Colour (0xff3b3c40)).withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ b.getX() + 8.0f, b.getCentreY() }));
            if (lit)
            {
                g.setColour (L::acid().withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ b.getX() + 8.0f, b.getCentreY() }));
            }
            g.setColour ((i % 4 == 0 ? juce::Colour (0xffe9e9e6) : juce::Colour (0xff9c9ea3)).withMultipliedAlpha (alpha));
            g.setFont (lf.font (11.0f, true, 0.05f));
            g.drawText (juce::String (i + 1), b.withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);
        }

        // Case de la note : petit écran
        {
            const auto b = cellBounds (i, noteRow).toFloat();
            g.setColour (L::lcd().withMultipliedAlpha (inPattern ? 1.0f : 0.6f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour ((i == playing ? L::acid() : juce::Colour (0xff3a3c40)).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, i == playing ? 1.6f : 1.0f);

            const auto inner = b.reduced (4.0f, 6.0f).withTrimmedBottom (18.0f);
            juce::String label;
            juce::Colour colour = accent ? L::hot() : L::acid();

            if (time == 0)
            {
                lastNoteStep = i;
                const float top = barTop (i);
                g.setColour (colour.withAlpha (0.22f * alpha));
                g.fillRect (juce::Rectangle<float> (inner.getX() + 6.0f, top, inner.getWidth() - 12.0f, inner.getBottom() - top));
                g.setColour (colour.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (juce::Rectangle<float> (inner.getX() + 4.0f, top - 2.0f, inner.getWidth() - 8.0f, 4.0f), 2.0f);
                label = pitchNames[pitch];
            }
            else if (time == 1)
            {
                label = "TIE";
                if (lastNoteStep >= 0)
                {
                    const float top = barTop (lastNoteStep);
                    g.setColour (L::acid().withAlpha (0.5f * alpha));
                    const float dash[] = { 4.0f, 3.0f };
                    g.drawDashedLine (juce::Line<float> (inner.getX(), top, inner.getRight(), top), dash, 2, 2.0f);
                }
            }
            else
            {
                label = "-";
            }

            // Slide : trait jusqu'à la note suivante
            if (slide && time != 2)
            {
                const int next = (i + 1) % length;
                if (next < AcidMachineAudioProcessor::numSteps && valueOf (steps[(size_t) next].time) == 0)
                {
                    const float y0 = time == 0 ? barTop (i) : (lastNoteStep >= 0 ? barTop (lastNoteStep) : inner.getBottom());
                    const float y1 = barTop (next);
                    g.setColour (L::acid().withAlpha (0.85f * alpha));
                    g.drawLine (inner.getRight() - 4.0f, y0, inner.getRight() + 9.0f, y1, 2.0f);
                }
            }

            auto text = b.withTrimmedTop (b.getHeight() - 20.0f);
            g.setColour ((time == 0 ? juce::Colour (0xfff2f2ee) : juce::Colour (0xff8d9095)).withMultipliedAlpha (alpha));
            g.setFont (lf.font (time == 0 ? 14.0f : 11.0f, true, 0.04f));
            g.drawText (label, text, juce::Justification::centred, false);

            if (time == 0 && octave != 1)
            {
                juce::Path tri;
                const float tx = text.getRight() - 9.0f, ty = text.getCentreY();
                if (octave == 2) tri.addTriangle (tx - 4.0f, ty + 3.0f, tx + 4.0f, ty + 3.0f, tx, ty - 3.0f);
                else             tri.addTriangle (tx - 4.0f, ty - 3.0f, tx + 4.0f, ty - 3.0f, tx, ty + 3.0f);
                g.setColour (L::acid().withMultipliedAlpha (alpha));
                g.fillPath (tri);
            }
        }

        // Interrupteurs
        auto drawKey = [&] (Row row, bool on, juce::Colour litColour, const juce::String& text)
        {
            const auto b = cellBounds (i, row).toFloat();
            if (on)
                g.setGradientFill (juce::ColourGradient (litColour.brighter (0.25f), 0.0f, b.getY(), litColour, 0.0f, b.getBottom(), false));
            else
                g.setColour (juce::Colour (0xff3a3b3f));
            g.fillRoundedRectangle (b, 3.0f);
            if (! on)
            {
                g.setColour (juce::Colour (0xff4a4c50));
                g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            }
            if (! inPattern)
            {
                g.setColour (juce::Colour (0xff1b1c1e).withAlpha (0.65f));
                g.fillRoundedRectangle (b, 3.0f);
            }
            g.setColour (on ? L::ink() : juce::Colour (0xff8d9095).withMultipliedAlpha (alpha));
            g.setFont (lf.font (9.5f, true, 0.12f));
            g.drawText (text, b, juce::Justification::centred, false);
        };

        drawKey (upRow, octave == 2, juce::Colour (0xffe8e8e4), "UP");
        drawKey (downRow, octave == 0, juce::Colour (0xffe8e8e4), "DOWN");
        drawKey (timeRow, time != 2, time == 1 ? juce::Colour (0xff9fd8ff) : juce::Colour (0xffd9dadc), time == 0 ? "NOTE" : (time == 1 ? "TIE" : "REST"));
        drawKey (accentRow, accent, L::hot(), "ACC");
        drawKey (slideRow, slide, L::acid(), "SLIDE");
    }

    // Fin du motif
    if (length < AcidMachineAudioProcessor::numSteps)
    {
        const float x = (float) (firstColumnX + length * columnWidth);
        g.setColour (L::acid().withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, 6.0f, 2.0f, (float) getHeight() - 12.0f));
    }

    // Repères des temps
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int beat = 1; beat < 4; ++beat)
        g.fillRect (juce::Rectangle<float> ((float) (firstColumnX + beat * 4 * columnWidth) - 0.5f, 30.0f, 1.0f, (float) getHeight() - 38.0f));
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int column = 0;
    Row row = numberRow;
    dragColumn = -1;
    draggedParameter = nullptr;

    if (! cellAt (e.getPosition(), column, row))
        return;

    auto& s = steps[(size_t) column];

    switch (row)
    {
        case numberRow:
            // Clic sur un numéro : le motif s'arrête à ce pas
            setValue (lengthParameter, column + 1);
            break;

        case noteRow:
            if (e.mods.isPopupMenu())
            {
                showNoteMenu (column);
                return;
            }
            dragColumn = column;
            dragRow = noteRow;
            dragStartPitch = valueOf (s.pitch);
            draggedParameter = s.pitch;
            draggedParameter->beginChangeGesture();
            // Un clic sur un silence en fait une note
            if (valueOf (s.time) != 0)
                setValue (s.time, 0);
            break;

        case upRow:
            setValue (s.octave, valueOf (s.octave) == 2 ? 1 : 2);
            break;

        case downRow:
            setValue (s.octave, valueOf (s.octave) == 0 ? 1 : 0);
            break;

        case timeRow:
            setValue (s.time, (valueOf (s.time) + 1) % 3);
            break;

        case accentRow:
        case slideRow:
        {
            auto* p = row == accentRow ? s.accent : s.slide;
            paintValue = valueOf (p) != 0 ? 0 : 1;
            setValue (p, paintValue);
            dragColumn = column;
            dragRow = row;
            break;
        }

        case numRows:
        default:
            break;
    }

    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragColumn < 0)
        return;

    if (dragRow == noteRow && draggedParameter != nullptr)
    {
        const int pitch = juce::jlimit (0, 12, dragStartPitch + juce::roundToInt ((float) -e.getDistanceFromDragStartY() / 8.0f));
        if (pitch != valueOf (draggedParameter))
        {
            draggedParameter->setValueNotifyingHost (draggedParameter->convertTo0to1 ((float) pitch));
            repaint();
        }
        return;
    }

    if (dragRow == accentRow || dragRow == slideRow)
    {
        // On « peint » la rangée en glissant sur les cases voisines
        const int column = juce::jlimit (0, AcidMachineAudioProcessor::numSteps - 1, (e.x - firstColumnX) / columnWidth);
        auto* p = dragRow == accentRow ? steps[(size_t) column].accent : steps[(size_t) column].slide;
        if (valueOf (p) != paintValue)
        {
            setValue (p, paintValue);
            repaint();
        }
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (draggedParameter != nullptr)
        draggedParameter->endChangeGesture();
    draggedParameter = nullptr;
    dragColumn = -1;
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    int column = 0;
    Row row = numberRow;
    if (cellAt (e.getPosition(), column, row) && row == noteRow)
    {
        setValue (steps[(size_t) column].pitch, 0);
        repaint();
    }
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int column = 0;
    Row row = numberRow;
    if (! cellAt (e.getPosition(), column, row) || row != noteRow)
        return;

    const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (std::abs (delta) < 1.0e-4f)
        return;

    auto* p = steps[(size_t) column].pitch;
    setValue (p, juce::jlimit (0, 12, valueOf (p) + (delta > 0 ? 1 : -1)));
    repaint();
}

void StepGrid::showNoteMenu (int column)
{
    auto& s = steps[(size_t) column];
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lf);
    menu.addSectionHeader ("STEP " + juce::String (column + 1));

    const auto& names = AcidMachineAudioProcessor::pitchNames();
    const int current = valueOf (s.pitch);
    for (int n = 12; n >= 0; --n)
        menu.addItem (n + 1, names[n], true, n == current && valueOf (s.time) == 0);

    menu.addSeparator();
    menu.addItem (100, "TIE", true, valueOf (s.time) == 1);
    menu.addItem (101, "REST", true, valueOf (s.time) == 2);

    juce::Component::SafePointer<StepGrid> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellBounds (column, noteRow))),
                        [safe, column] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& st = safe->steps[(size_t) column];
                            if (result == 100)       setValue (st.time, 1);
                            else if (result == 101)  setValue (st.time, 2);
                            else
                            {
                                setValue (st.pitch, result - 1);
                                setValue (st.time, 0);
                            }
                            safe->repaint();
                        });
}

//==============================================================================
AcidMachineFacade::AcidMachineFacade (AcidMachineAudioProcessor& p)
    : audioProcessor (p),
      stepGrid (p, lookAndFeel),
      presetBar (p.presets, [this] (float h, bool bold, float tracking) { return lookAndFeel.font (h, bold, tracking); })
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);

    using P = AcidMachineAudioProcessor;
    addKnob (tuningKnob,    P::tuningId,    knobBounds (knobX[0], knobY, bigKnob), "big");
    addKnob (cutoffKnob,    P::cutoffId,    knobBounds (knobX[1], knobY, bigKnob), "big");
    addKnob (resonanceKnob, P::resonanceId, knobBounds (knobX[2], knobY, bigKnob), "big");
    addKnob (envModKnob,    P::envModId,    knobBounds (knobX[3], knobY, bigKnob), "big");
    addKnob (decayKnob,     P::decayId,     knobBounds (knobX[4], knobY, bigKnob), "big");
    addKnob (accentKnob,    P::accentId,    knobBounds (knobX[5], knobY, bigKnob), "big");
    addKnob (driveKnob,     P::driveId,     knobBounds (driveX, knobY, outKnob), "mid");
    addKnob (slideKnob,     P::slideTimeId, knobBounds (slideX, knobY, outKnob), "mid");
    addKnob (volumeKnob,    P::volumeId,    knobBounds (volumeX, knobY, outKnob), "mid");
    addKnob (lengthKnob,    P::lengthId,    knobBounds (lengthX, seqControlsY, miniKnob), "mini");
    addKnob (swingKnob,     P::swingId,     knobBounds (swingX, seqControlsY, miniKnob), "mini");
    addKnob (transposeKnob, P::transposeId, knobBounds (transposeX, seqControlsY, miniKnob), "mini");

    addSwitch (waveformSwitch, P::waveformId, { "SAW", "SQUARE" }, { 786, 152, 82, 76 }, true);

    // Départ de la séquence : lecture du DAW (HOST) ou note MIDI tenue (NOTE)
    addSwitch (triggerSwitch, P::seqTriggerId, { "HOST", "NOTE" }, { 116, seqControlsY - 18, 54, 36 }, true);
    for (auto* b : triggerSwitch.buttons)
        b->setTitle ("Sequencer starts with " + b->getButtonText());

    // Mode du DRIVE (menu)
    driveModeButton.setComponentID ("driveMode");
    driveModeButton.setBounds (driveX - 40, 246, 80, 22);
    driveModeButton.setTitle ("Drive mode");
    driveModeButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    driveModeButton.onClick = [this] { showDriveModeMenu(); };
    addAndMakeVisible (driveModeButton);
    if (auto* modeParam = p.apvts.getParameter (P::driveModeId))
    {
        driveModeAttachment = std::make_unique<juce::ParameterAttachment> (*modeParam, [this] (float v)
        {
            static const char* names[] = { "SOFT", "DIODE 1", "DIODE 2" };
            driveModeButton.setButtonText (names[juce::jlimit (0, 2, juce::roundToInt (v))]);
        });
        driveModeAttachment->sendInitialUpdate();
    }

    // RANDOM : style (menu) + bouton
    styleButton.setComponentID ("randomStyle");
    styleButton.setBounds (700, seqControlsY - 18, 190, 36);
    styleButton.setTitle ("Random style");
    styleButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    styleButton.onClick = [this] { showStyleMenu(); };
    addAndMakeVisible (styleButton);

    randomButton.setComponentID ("random");
    randomButton.setBounds (900, seqControlsY - 18, 176, 36);
    randomButton.setTitle ("Random pattern");
    randomButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    randomButton.onClick = [this] { audioProcessor.randomizePattern(); stepGrid.repaint(); };
    addAndMakeVisible (randomButton);

    if (auto* styleParam = p.apvts.getParameter (P::randomStyleId))
    {
        styleAttachment = std::make_unique<juce::ParameterAttachment> (*styleParam, [this] (float v)
        {
            styleButton.setButtonText (acid::PatternGenerator::styleName (juce::roundToInt (v)));
        });
        styleAttachment->sendInitialUpdate();
    }
    addSwitch (rateSwitch, P::rateId, P::rateNames(), { 176, seqControlsY - 16, 248, 32 }, false);

    seqButton.setClickingTogglesState (true);
    seqButton.setComponentID ("seq");
    seqButton.setBounds (40, seqControlsY - 18, 70, 36);
    seqButton.setTitle ("Sequencer on / off");
    seqButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (seqButton);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (p.apvts, P::seqId, seqButton));

    stepGrid.setBounds (14, gridTop, W - 28, gridBottom - gridTop);
    addAndMakeVisible (stepGrid);

    presetBar.setBounds (W - 28 - phragma::PresetBar::idealWidth - 92, 18, phragma::PresetBar::idealWidth, 28);
    addAndMakeVisible (presetBar);
    presetBar.attachSavePanelTo (*this);

    startTimerHz (30);
}

AcidMachineFacade::~AcidMachineFacade()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AcidMachineFacade::addKnob (juce::Slider& s, const char* parameterId, juce::Rectangle<int> bounds, const char* size)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (juce::String (size) == "mini" ? 160 : 220);
    s.setVelocityBasedMode (false);
    s.getProperties().set ("size", size);
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.setBounds (bounds);
    addAndMakeVisible (s);

    auto* param = audioProcessor.apvts.getParameter (parameterId);
    s.setTitle (param->getName (64));
    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, parameterId, s));
    s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    knobLabels.push_back ({ &s, parameterId });
}

void AcidMachineFacade::addSwitch (ChoiceSwitch& sw, const char* parameterId, const juce::StringArray& texts, juce::Rectangle<int> area, bool vertical)
{
    auto* param = audioProcessor.apvts.getParameter (parameterId);
    const int n = texts.size();

    for (int i = 0; i < n; ++i)
    {
        auto* b = sw.buttons.add (new juce::TextButton (texts[i]));
        b->setComponentID (juce::String (parameterId) + juce::String (i));
        b->setClickingTogglesState (false);
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        b->setTitle (param->getName (64) + " " + texts[i]);

        juce::Rectangle<int> r;
        if (vertical)
        {
            const int h = (area.getHeight() - (n - 1) * 6) / n;
            r = { area.getX(), area.getY() + i * (h + 6), area.getWidth(), h };
        }
        else
        {
            const int w = (area.getWidth() - (n - 1) * 4) / n;
            r = { area.getX() + i * (w + 4), area.getY(), w, area.getHeight() };
        }
        b->setBounds (r);
        addAndMakeVisible (b);
    }

    auto* swPtr = &sw;
    sw.attachment = std::make_unique<juce::ParameterAttachment> (*param, [swPtr] (float value)
    {
        const int index = juce::roundToInt (value);
        for (int i = 0; i < swPtr->buttons.size(); ++i)
            swPtr->buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    });

    for (int i = 0; i < n; ++i)
        sw.buttons[i]->onClick = [swPtr, i] { swPtr->attachment->setValueAsCompleteGesture ((float) i); };

    sw.attachment->sendInitialUpdate();
}

void AcidMachineFacade::showDriveModeMenu()
{
    auto* param = audioProcessor.apvts.getParameter (AcidMachineAudioProcessor::driveModeId);
    const int current = juce::roundToInt (param->convertFrom0to1 (param->getValue()));

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addSectionHeader ("DRIVE MODE");
    menu.addItem (1, "SOFT", true, current == 0);
    menu.addItem (2, "DIODE 1  (ASYMMETRIC)", true, current == 1);
    menu.addItem (3, "DIODE 2  (RC CLIPPER)", true, current == 2);

    juce::Component::SafePointer<AcidMachineFacade> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&driveModeButton).withMinimumWidth (160),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->driveModeAttachment->setValueAsCompleteGesture ((float) (result - 1));
                        });
}

void AcidMachineFacade::showStyleMenu()
{
    auto* param = audioProcessor.apvts.getParameter (AcidMachineAudioProcessor::randomStyleId);
    const int current = juce::roundToInt (param->convertFrom0to1 (param->getValue()));

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addSectionHeader ("RANDOM STYLE");
    for (int i = 0; i < acid::PatternGenerator::numStyles; ++i)
        menu.addItem (i + 1, acid::PatternGenerator::styleName (i), true, i == current);

    juce::Component::SafePointer<AcidMachineFacade> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleButton).withMinimumWidth (styleButton.getWidth()),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            safe->styleAttachment->setValueAsCompleteGesture ((float) (result - 1));
                        });
}

void AcidMachineFacade::timerCallback()
{
    const bool accentNow = audioProcessor.getSoundingNote() >= 0 && audioProcessor.isSoundingAccent();
    const bool noteNow = audioProcessor.getSoundingNote() >= 0;
    const int step = audioProcessor.isSequencerRunning() ? audioProcessor.getPlayingStep() : -1;

    if (accentNow != accentLit || noteNow != noteLit)
    {
        accentLit = accentNow;
        noteLit = noteNow;
        repaint (juce::Rectangle<int> (knobX[5] - 60, 96, 140, 30));
        repaint (juce::Rectangle<int> (20, 96, 160, 30));
    }

    // Valeurs sous les potards, grille (motif modifié par l'automation, pas en cours)
    repaint (juce::Rectangle<int> (0, voiceTop + 4, W, voiceBottom - voiceTop - 8));
    repaint (juce::Rectangle<int> (0, seqRowTop + 2, W, gridTop - seqRowTop - 4));

    if (step != shownStep)
        shownStep = step;
    stepGrid.repaint();
}

//==============================================================================
void AcidMachineFacade::paint (juce::Graphics& g)
{
    const float scale = juce::jlimit (0.5f, 4.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

    if (! background.isValid() || std::abs (scale - backgroundScale) > 0.01f)
    {
        backgroundScale = scale;
        background = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) W * scale), juce::roundToInt ((float) H * scale), true);
        juce::Graphics layer (background);
        layer.addTransform (juce::AffineTransform::scale (scale));
        paintBackground (layer);
    }

    g.drawImage (background, getLocalBounds().toFloat());
    paintDynamic (g);
}

void AcidMachineFacade::paintBackground (juce::Graphics& g)
{
    using L = AcidLookAndFeel;
    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) W, (float) H);

    // Aluminium brossé
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdcdedf), 0.0f, 0.0f, juce::Colour (0xffb7babe), 0.0f, (float) H, false));
    g.fillRect (full);
    {
        juce::Random random (303);
        for (int y = 0; y < H; ++y)
        {
            const float v = random.nextFloat();
            g.setColour ((v > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + 0.03f * std::abs (v - 0.5f)));
            g.fillRect (0.0f, (float) y, (float) W, 1.0f);
        }
    }

    // Bandeau noir
    g.setColour (juce::Colour (0xff111111));
    g.fillRect (0.0f, 0.0f, (float) W, (float) headerHeight);
    g.setColour (L::acid());
    g.fillRect (0.0f, (float) headerHeight - 3.0f, (float) W, 3.0f);

    L::drawSmiley (g, { 22.0f, 8.0f, 46.0f, 46.0f });

    g.setColour (juce::Colour (0xfff4f4f0));
    g.setFont (lookAndFeel.font (27.0f, true, 0.14f));
    g.drawText ("ACID MACHINE", juce::Rectangle<float> (82.0f, 10.0f, 330.0f, 30.0f), juce::Justification::centredLeft, false);
    g.setColour (L::acid());
    g.setFont (lookAndFeel.font (10.0f, true, 0.38f));
    g.drawText ("BY PHRAGMA", juce::Rectangle<float> (84.0f, 38.0f, 200.0f, 14.0f), juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xff8e9196));
    g.setFont (lookAndFeel.font (11.0f, true, 0.12f));
    g.drawText ("V" ACIDMACHINE_VERSION, juce::Rectangle<float> ((float) W - 112.0f, 18.0f, 84.0f, 28.0f), juce::Justification::centredRight, false);

    // Étiquettes imprimées de la voix
    auto label = [&] (const juce::String& text, int cx, int y, float size = 11.0f)
    {
        g.setColour (L::ink());
        g.setFont (lookAndFeel.font (size, true, 0.12f));
        g.drawText (text, juce::Rectangle<float> ((float) cx - 70.0f, (float) y, 140.0f, 16.0f), juce::Justification::centred, false);
    };

    const char* names[] = { "TUNING", "CUT OFF FREQ", "RESONANCE", "ENV MOD", "DECAY", "ACCENT" };
    for (int i = 0; i < 6; ++i)
    {
        label (names[i], knobX[i], 118);

        // Graduations autour du potard
        const auto c = juce::Point<float> ((float) knobX[i], (float) knobY);
        for (int t = 0; t <= 10; ++t)
        {
            const float a = juce::MathConstants<float>::pi * (1.25f + 1.5f * (float) t / 10.0f);
            const auto p0 = c.getPointOnCircumference (bigKnob * 0.5f + 2.0f, a);
            const auto p1 = c.getPointOnCircumference (bigKnob * 0.5f + (t % 5 == 0 ? 9.0f : 6.0f), a);
            g.setColour (L::ink().withAlpha (0.75f));
            g.drawLine ({ p0, p1 }, t % 5 == 0 ? 1.6f : 1.1f);
        }
    }

    label ("WAVEFORM", 827, 118);
    label ("DRIVE", driveX, 118);
    label ("SLIDE", slideX, 118);
    label ("VOLUME", volumeX, 118);

    // Filets
    g.setColour (L::ink().withAlpha (0.8f));
    g.fillRect (24.0f, 104.0f, 734.0f, 2.0f);
    g.fillRect (780.0f, 104.0f, 296.0f, 2.0f);
    g.setColour (L::ink());
    g.setFont (lookAndFeel.font (9.5f, true, 0.42f));
    g.drawText ("VOICE", juce::Rectangle<float> (24.0f, 82.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);
    g.drawText ("OUTPUT", juce::Rectangle<float> (780.0f, 82.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);

    // Séparation voix / séquenceur
    g.setColour (L::ink().withAlpha (0.85f));
    g.fillRect (24.0f, (float) voiceBottom, (float) W - 48.0f, 2.0f);
    g.setColour (L::ink());
    g.setFont (lookAndFeel.font (9.5f, true, 0.42f));
    g.drawText ("PATTERN", juce::Rectangle<float> (24.0f, (float) voiceBottom + 6.0f, 200.0f, 16.0f), juce::Justification::centredLeft, false);

    label ("RATE", 300, seqRowTop + 18, 10.0f);
    label ("LENGTH", lengthX, seqRowTop + 18, 10.0f);
    label ("SWING", swingX, seqRowTop + 18, 10.0f);
    label ("TRANSPOSE", transposeX, seqRowTop + 18, 10.0f);

    label ("RANDOM STYLE", 795, seqRowTop + 18, 10.0f);
    label ("NEW PATTERN", 988, seqRowTop + 18, 10.0f);

    // Aide
    g.setColour (L::dimInk());
    g.setFont (lookAndFeel.font (9.0f, true, 0.08f));
    g.drawText ("SEQ OFF : PLAY FROM MIDI, VELOCITY 110+ = ACCENT, OVERLAP = SLIDE.   SEQ ON : NOTE = PATTERN RUNS WHILE A MIDI NOTE IS HELD (C3 = NO SHIFT), HOST = RUNS WITH DAW PLAY",
                juce::Rectangle<float> (24.0f, (float) voiceBottom - 24.0f, (float) W - 48.0f, 14.0f), juce::Justification::centredRight, false);
}

void AcidMachineFacade::paintDynamic (juce::Graphics& g)
{
    using L = AcidLookAndFeel;

    // Valeurs sous les potards
    g.setFont (lookAndFeel.font (10.5f, true, 0.06f));
    for (const auto& k : knobLabels)
    {
        auto* param = audioProcessor.apvts.getParameter (k.parameterId);
        const auto b = k.slider->getBounds();
        const bool mini = k.slider->getProperties()["size"].toString() == "mini";
        const auto area = juce::Rectangle<int> (b.getCentreX() - 50, b.getBottom() - (mini ? 2 : 0), 100, 16);
        g.setColour (k.slider->isMouseOverOrDragging() ? L::ink() : L::dimInk());
        g.drawText (param->getCurrentValueAsText(), area, juce::Justification::centred, false);
    }

    // Voyants NOTE et ACCENT
    auto led = [&] (juce::Point<float> c, bool on, juce::Colour colour)
    {
        if (on)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (c));
        }
        g.setColour (on ? colour : juce::Colour (0xff6d2a18).withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c), 0.8f);
    };

    led ({ (float) knobX[5] + 50.0f, 126.0f + 0.0f - 0.0f }, accentLit, L::hot());
    led ({ (float) knobX[0] + 46.0f, 126.0f }, noteLit, L::acid());
}

//==============================================================================
AcidMachineAudioProcessorEditor::AcidMachineAudioProcessorEditor (AcidMachineAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    // Taille gardée avec le projet (lue avant de régler les limites : les régler redimensionne déjà la fenêtre).
    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", AcidMachineFacade::width);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (AcidMachineFacade::width * 0.6), juce::roundToInt (AcidMachineFacade::height * 0.6),
                     juce::roundToInt (AcidMachineFacade::width * 1.5), juce::roundToInt (AcidMachineFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) AcidMachineFacade::width / (double) AcidMachineFacade::height);

    facade.getPresetBar().getScale = [this] { return getUiScale(); };
    facade.getPresetBar().setScale = [this] (float scale) { setUiScale (scale); };

    const int shownWidth = juce::jlimit (juce::roundToInt (AcidMachineFacade::width * 0.6), juce::roundToInt (AcidMachineFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * AcidMachineFacade::height / AcidMachineFacade::width));
    sized = true;
    resized();
}

AcidMachineAudioProcessorEditor::~AcidMachineAudioProcessorEditor() = default;

void AcidMachineAudioProcessorEditor::setUiScale (float scale)
{
    scale = juce::jlimit (0.6f, 1.5f, scale);
    setSize (juce::roundToInt ((float) AcidMachineFacade::width * scale), juce::roundToInt ((float) AcidMachineFacade::height * scale));
}

void AcidMachineAudioProcessorEditor::resized()
{
    facade.setTransform (juce::AffineTransform::scale (getUiScale()));
    facade.getPresetBar().refresh();

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
