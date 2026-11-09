#include "PluginProcessor.h"
#include "PluginEditor.h"

EmotionMorphProcessor::EmotionMorphProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "EmotionMorph", createParameterLayout())
{
    moodXParam      = state.getRawParameterValue (ParamIDs::moodX);
    moodYParam      = state.getRawParameterValue (ParamIDs::moodY);
    intensityParam  = state.getRawParameterValue (ParamIDs::intensity);
    mixParam        = state.getRawParameterValue (ParamIDs::mix);
    outputGainParam = state.getRawParameterValue (ParamIDs::outputGain);

    for (auto& w : meters.weights)
        w.store (0.25f);
}

juce::AudioProcessorValueTreeState::ParameterLayout EmotionMorphProcessor::createParameterLayout()
{
    using namespace juce;

    auto percent = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
                       .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; });

    auto position = AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([] (float v, int) { return String (v, 2); });

    return {
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::moodX, 1 }, "Mood X",
                                               NormalisableRange<float> (-1.0f, 1.0f), 0.0f, position),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::moodY, 1 }, "Mood Y",
                                               NormalisableRange<float> (-1.0f, 1.0f), 0.0f, position),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::intensity, 1 }, "Intensity",
                                               NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                               NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::outputGain, 1 }, "Output Gain",
                                               NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("dB")),
    };
}

//==============================================================================
bool EmotionMorphProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void EmotionMorphProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setControls ({ moodXParam->load(), moodYParam->load(), intensityParam->load(),
                          mixParam->load(), outputGainParam->load() });
    engine.prepare (sampleRate, samplesPerBlock);

    monoScratch.setSize (1, samplesPerBlock, false, true, true);

    const auto sr = (float) sampleRate;
    fastCoeff = std::exp (-1.0f / (0.002f * sr));
    slowCoeff = std::exp (-1.0f / (0.060f * sr));
    rmsCoeff  = std::exp (-1.0f / (0.120f * sr));
    transientHoldoffSamples = (int) (0.08f * sr);
    fastEnv = slowEnv = rmsEnv = 0.0f;
    transientHoldoff = 0;
}

void EmotionMorphProcessor::reset()
{
    engine.reset();
}

void EmotionMorphProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numOut == 0 || numSamples == 0)
        return;

    // Mono in -> stereo out: spread the input to both sides before processing.
    if (numIn == 1 && numOut == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    updateMeters (buffer, numSamples);

    engine.setControls ({ moodXParam->load(), moodYParam->load(), intensityParam->load(),
                          mixParam->load(), outputGainParam->load() });

    if (numOut >= 2)
    {
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    }
    else
    {
        // Mono track: run the stereo engine on a copy and fold back down.
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize (1, numSamples, false, false, true);

        auto* left = buffer.getWritePointer (0);
        auto* right = monoScratch.getWritePointer (0);
        std::copy (left, left + numSamples, right);
        engine.process (left, right, numSamples);

        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    }

    const auto& w = engine.getCurrentWeights();
    for (size_t i = 0; i < (size_t) emo::numMoods; ++i)
        meters.weights[i].store (w[i], std::memory_order_relaxed);
    meters.amount.store (engine.getCurrentAmount(), std::memory_order_relaxed);
}

void EmotionMorphProcessor::updateMeters (const juce::AudioBuffer<float>& input, int numSamples) noexcept
{
    // Loudness (for the orb's swelling) and transient detection (for ripples),
    // taken from the input so the orb reacts to what you play, not to the effect.
    const auto* l = input.getReadPointer (0);
    const auto* r = input.getNumChannels() > 1 ? input.getReadPointer (1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = 0.5f * (std::abs (l[i]) + std::abs (r[i]));

        fastEnv = x + fastCoeff * (fastEnv - x);
        slowEnv = x + slowCoeff * (slowEnv - x);
        rmsEnv = x * x + rmsCoeff * (rmsEnv - x * x);

        if (transientHoldoff > 0)
            --transientHoldoff;
        else if (fastEnv > 0.02f && fastEnv > 2.0f * slowEnv)
        {
            meters.transients.fetch_add (1, std::memory_order_relaxed);
            transientHoldoff = transientHoldoffSamples;
        }
    }

    meters.level.store (std::sqrt (rmsEnv), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* EmotionMorphProcessor::createEditor()
{
    return new EmotionMorphEditor (*this);
}

void EmotionMorphProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EmotionMorphProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EmotionMorphProcessor();
}
