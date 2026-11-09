#include "PluginEditor.h"

namespace
{
    const juce::Colour background { 0xff0b0b0d };
    const juce::Colour ringColour { 0xff2a2a30 };
    const juce::Colour textColour { 0xffb8b8c0 };

    // Joy, Melancholy, Tension, Dream
    const std::array<juce::Colour, emo::numMoods> moodColours {
        juce::Colour (0xffffc857),   // warm gold
        juce::Colour (0xff2f5fd0),   // deep blue
        juce::Colour (0xffe0443e),   // red
        juce::Colour (0xffb9a3ff),   // lavender
    };

    const std::array<const char*, emo::numMoods> moodNames { "JOY", "MELANCHOLY", "TENSION", "DREAM" };

    juce::Colour blendColour (const std::array<float, emo::numMoods>& w, float amount)
    {
        float r = 0, g = 0, b = 0;
        for (size_t i = 0; i < (size_t) emo::numMoods; ++i)
        {
            r += w[i] * moodColours[i].getFloatRed();
            g += w[i] * moodColours[i].getFloatGreen();
            b += w[i] * moodColours[i].getFloatBlue();
        }
        const auto mood = juce::Colour::fromFloatRGBA (r, g, b, 1.0f);
        return juce::Colours::white.withAlpha (0.85f).interpolatedWith (mood, juce::jlimit (0.0f, 1.0f, amount * 1.5f));
    }
}

//==============================================================================
MoodPad::MoodPad (juce::AudioProcessorValueTreeState& state)
    : xAttachment (*state.getParameter (ParamIDs::moodX), [this] (float v) { x = v; repaint(); }),
      yAttachment (*state.getParameter (ParamIDs::moodY), [this] (float v) { y = v; repaint(); })
{
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
}

juce::Rectangle<float> MoodPad::field() const
{
    const auto size = (float) juce::jmin (getWidth(), getHeight()) - 90.0f;
    return juce::Rectangle<float> (size, size).withCentre (getLocalBounds().toFloat().getCentre());
}

void MoodPad::paint (juce::Graphics& g)
{
    const auto f = field();
    const auto c = f.getCentre();
    const auto radius = f.getWidth() * 0.5f;

    // Computed from the pad position so the colours follow the orb even when
    // no audio is running.
    const auto weights = emo::computeMoodWeights ({ x, y });
    const auto amount = emo::distanceFromCentre ({ x, y });

    g.setColour (ringColour);
    g.drawEllipse (f, 1.0f);

    // Mood names around the ring, brighter as the orb nears them.
    g.setFont (juce::FontOptions (13.0f));
    for (size_t i = 0; i < (size_t) emo::numMoods; ++i)
    {
        const auto anchor = emo::moodAnchors[i];
        const juce::Point<float> pos { c.x + anchor.x * (radius + 34.0f), c.y - anchor.y * (radius + 22.0f) };
        const auto glow = juce::jlimit (0.0f, 1.0f, weights[i] * amount * 1.6f);
        g.setColour (textColour.withAlpha (0.35f).interpolatedWith (moodColours[i], glow));
        g.drawText (moodNames[i], juce::Rectangle<float> (110.0f, 20.0f).withCentre (pos),
                    juce::Justification::centred);
    }

    // Placeholder orb.
    const juce::Point<float> orb { c.x + x * radius, c.y - y * radius };
    const auto colour = blendColour (weights, amount);
    const auto orbRadius = 14.0f + 10.0f * juce::jlimit (0.0f, 1.0f, level * 4.0f);

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), orb,
                                             colour.withAlpha (0.0f), orb.translated (orbRadius * 2.5f, 0), true));
    g.fillEllipse (juce::Rectangle<float> (orbRadius * 5.0f, orbRadius * 5.0f).withCentre (orb));
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (orbRadius * 2.0f, orbRadius * 2.0f).withCentre (orb));
}

void MoodPad::moveTo (juce::Point<float> mousePos)
{
    const auto f = field();
    const auto radius = f.getWidth() * 0.5f;
    const auto p = emo::clampToCircle ({ (mousePos.x - f.getCentreX()) / radius,
                                         (f.getCentreY() - mousePos.y) / radius });
    xAttachment.setValueAsPartOfGesture (p.x);
    yAttachment.setValueAsPartOfGesture (p.y);
}

void MoodPad::mouseDown (const juce::MouseEvent& e)
{
    xAttachment.beginGesture();
    yAttachment.beginGesture();
    moveTo (e.position);
}

void MoodPad::mouseDrag (const juce::MouseEvent& e) { moveTo (e.position); }

void MoodPad::mouseUp (const juce::MouseEvent&)
{
    xAttachment.endGesture();
    yAttachment.endGesture();
}

void MoodPad::mouseDoubleClick (const juce::MouseEvent&)
{
    xAttachment.setValueAsCompleteGesture (0.0f);
    yAttachment.setValueAsCompleteGesture (0.0f);
}

void MoodPad::setLevel (float newLevel)
{
    if (std::abs (newLevel - level) > 1.0e-4f)
    {
        level = newLevel;
        repaint();
    }
}

//==============================================================================
EmotionMorphEditor::EmotionMorphEditor (EmotionMorphProcessor& p)
    : AudioProcessorEditor (&p), processor (p), pad (p.getState())
{
    addAndMakeVisible (pad);
    setUpSlider (intensity, "Intensity", ParamIDs::intensity);
    setUpSlider (mix, "Mix", ParamIDs::mix);
    setUpSlider (output, "Output", ParamIDs::outputGain);

    setSize (520, 600);
    startTimerHz (30);
}

void EmotionMorphEditor::setUpSlider (LabelledSlider& s, const juce::String& name, const char* paramID)
{
    s.label.setText (name, juce::dontSendNotification);
    s.label.setColour (juce::Label::textColourId, textColour);
    s.label.setJustificationType (juce::Justification::centredLeft);

    s.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    s.slider.setColour (juce::Slider::trackColourId, textColour.withAlpha (0.5f));
    s.slider.setColour (juce::Slider::backgroundColourId, ringColour);
    s.slider.setColour (juce::Slider::thumbColourId, textColour);
    s.slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    s.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    s.attachment = std::make_unique<SliderAttachment> (processor.getState(), paramID, s.slider);

    addAndMakeVisible (s.label);
    addAndMakeVisible (s.slider);
}

void EmotionMorphEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void EmotionMorphEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto bottom = area.removeFromBottom (90);
    pad.setBounds (area);

    for (auto* s : { &intensity, &mix, &output })
    {
        auto row = bottom.removeFromTop (28);
        s->label.setBounds (row.removeFromLeft (80));
        s->slider.setBounds (row);
    }
}

void EmotionMorphEditor::timerCallback()
{
    pad.setLevel (processor.getMeters().level.load (std::memory_order_relaxed));
}
