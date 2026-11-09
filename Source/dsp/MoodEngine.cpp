#include "MoodEngine.h"

namespace emo
{

void MoodEngine::prepare (double newSampleRate, int /*maximumBlockSize*/)
{
    sampleRate = newSampleRate;

    smoothX.reset (sampleRate, 0.03);
    smoothY.reset (sampleRate, 0.03);
    smoothIntensity.reset (sampleRate, 0.05);
    smoothMix.reset (sampleRate, 0.05);
    smoothGain.reset (sampleRate, 0.05);

    saturation.prepare (sampleRate);
    filter.prepare (sampleRate);
    modDelay.prepare (sampleRate);
    tremolo.prepare (sampleRate);
    pitch.prepare (sampleRate);
    echo.prepare (sampleRate);
    reverb.prepare (sampleRate);
    compressor.prepare (sampleRate);

    reset();
}

void MoodEngine::reset()
{
    // Jump straight to the current controls so playback never starts with a sweep.
    smoothX.setCurrentAndTargetValue (controls.moodX);
    smoothY.setCurrentAndTargetValue (controls.moodY);
    smoothIntensity.setCurrentAndTargetValue (controls.intensity);
    smoothMix.setCurrentAndTargetValue (controls.mix);
    smoothGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (controls.outputGainDb));

    saturation.reset();
    filter.reset();
    modDelay.reset();
    tremolo.reset();
    pitch.reset();
    echo.reset();
    reverb.reset();
    compressor.reset();

    updateMoodTargets();

    saturation.snap();
    filter.snap();
    modDelay.snap();
    tremolo.snap();
    pitch.snap();
    echo.snap();
    reverb.snap();
    compressor.snap();
}

void MoodEngine::updateMoodTargets() noexcept
{
    const Point p { smoothX.getCurrentValue(), smoothY.getCurrentValue() };

    weights = computeMoodWeights (p);
    amountScale = distanceFromCentre (p) * juce::jlimit (0.0f, 1.0f, smoothIntensity.getCurrentValue());
    voice = blendMoods (weights, amountScale);

    saturation.setTargets (voice);
    filter.setTargets (voice);
    modDelay.setTargets (voice);
    tremolo.setTargets (voice);
    pitch.setTargets (voice);
    echo.setTargets (voice);
    reverb.setTargets (voice);
    compressor.setTargets (voice);
}

void MoodEngine::process (float* left, float* right, int numSamples) noexcept
{
    jassert (left != right);

    smoothX.setTargetValue (juce::jlimit (-1.0f, 1.0f, controls.moodX));
    smoothY.setTargetValue (juce::jlimit (-1.0f, 1.0f, controls.moodY));
    smoothIntensity.setTargetValue (juce::jlimit (0.0f, 1.0f, controls.intensity));
    smoothMix.setTargetValue (juce::jlimit (0.0f, 1.0f, controls.mix));
    smoothGain.setTargetValue (juce::Decibels::decibelsToGain (controls.outputGainDb));

    for (int start = 0; start < numSamples; start += controlBlockSize)
    {
        const auto n = std::min (controlBlockSize, numSamples - start);
        auto* l = left + start;
        auto* r = right + start;

        // Control-rate update: advance the orb smoothers and retarget every stage.
        smoothX.skip (n);
        smoothY.skip (n);
        smoothIntensity.skip (n);
        updateMoodTargets();

        std::copy (l, l + n, dryL.begin());
        std::copy (r, r + n, dryR.begin());

        saturation.process (l, r, n);
        filter.process (l, r, n);
        modDelay.process (l, r, n);
        tremolo.process (l, r, n);
        pitch.process (l, r, n);
        echo.process (l, r, n);
        reverb.process (l, r, n);
        compressor.process (l, r, n);

        for (int i = 0; i < n; ++i)
        {
            const auto m = smoothMix.getNextValue();
            const auto g = smoothGain.getNextValue();
            l[i] = g * (dryL[(size_t) i] + m * (l[i] - dryL[(size_t) i]));
            r[i] = g * (dryR[(size_t) i] + m * (r[i] - dryR[(size_t) i]));
        }
    }
}

} // namespace emo
