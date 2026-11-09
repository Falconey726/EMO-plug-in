#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

/** Placeholder mood pad: a circle you can drag a dot around. The glowing orb,
    particle trails and audio-reactive breathing come in the UI pass. */
class MoodPad : public juce::Component
{
public:
    MoodPad (juce::AudioProcessorValueTreeState& state);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void setLevel (float newLevel);

private:
    juce::Rectangle<float> field() const;
    void moveTo (juce::Point<float> mousePos);

    juce::ParameterAttachment xAttachment, yAttachment;
    float x = 0.0f, y = 0.0f;
    float level = 0.0f;
};

class EmotionMorphEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit EmotionMorphEditor (EmotionMorphProcessor&);
    ~EmotionMorphEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct LabelledSlider
    {
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setUpSlider (LabelledSlider&, const juce::String& name, const char* paramID);

    EmotionMorphProcessor& processor;
    MoodPad pad;
    LabelledSlider intensity, mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmotionMorphEditor)
};
