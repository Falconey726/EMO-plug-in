#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#include "dsp/MoodEngine.h"

namespace ParamIDs
{
    inline constexpr auto moodX      = "moodX";
    inline constexpr auto moodY      = "moodY";
    inline constexpr auto intensity  = "intensity";
    inline constexpr auto mix        = "mix";
    inline constexpr auto outputGain = "outputGain";
}

class EmotionMorphProcessor : public juce::AudioProcessor
{
public:
    EmotionMorphProcessor();
    ~EmotionMorphProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    /** Live values for the UI (the orb). Written on the audio thread, read on the
        message thread. */
    struct Meters
    {
        std::array<std::atomic<float>, emo::numMoods> weights {};
        std::atomic<float> amount { 0.0f };   // distance from centre * intensity
        std::atomic<float> level { 0.0f };    // smoothed RMS of the input, linear
        std::atomic<int> transients { 0 };    // increments on each sharp attack
    };

    const Meters& getMeters() const noexcept { return meters; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void updateMeters (const juce::AudioBuffer<float>& input, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState state;

    std::atomic<float>* moodXParam = nullptr;
    std::atomic<float>* moodYParam = nullptr;
    std::atomic<float>* intensityParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    emo::MoodEngine engine;
    juce::AudioBuffer<float> monoScratch;

    Meters meters;
    float fastEnv = 0.0f, slowEnv = 0.0f, rmsEnv = 0.0f;
    float fastCoeff = 0.0f, slowCoeff = 0.0f, rmsCoeff = 0.0f;
    int transientHoldoff = 0, transientHoldoffSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmotionMorphProcessor)
};
