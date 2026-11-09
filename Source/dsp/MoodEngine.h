#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "MoodMap.h"
#include "Stages.h"

namespace emo
{

/** The complete Emotion Morph signal chain, independent of any plugin host.

    Input -> Saturation -> Filter -> Chorus/Vibrato -> Tremolo -> Pitch
          -> Delay -> Reverb -> Compressor -> Mix -> Output

    The host-facing controls (orb X/Y, Intensity, Mix, Output gain) are smoothed
    per sample. Every 32 samples the smoothed orb position is turned into mood
    weights and a blended MoodVoice, whose values become the targets for each
    stage's own per-sample smoothers. That double smoothing keeps fast orb moves
    and automation free of zipper noise.
*/
class MoodEngine
{
public:
    struct Controls
    {
        float moodX = 0.0f;        // -1 (Tension) .. +1 (Dream)
        float moodY = 0.0f;        // -1 (Melancholy) .. +1 (Joy)
        float intensity = 1.0f;    // 0..1
        float mix = 1.0f;          // 0..1 dry/wet
        float outputGainDb = 0.0f; // dB
    };

    void prepare (double sampleRate, int maximumBlockSize);
    void reset();

    /** Sets new targets. Safe to call once per block from the audio thread. */
    void setControls (const Controls& c) noexcept { controls = c; }

    /** Processes a stereo block in place. The two pointers must be different;
        for mono material, copy the channel into a second buffer first. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** The most recently computed weights and settings (audio thread only;
        the processor copies them into atomics for the UI). */
    const MoodWeights& getCurrentWeights() const noexcept { return weights; }
    float getCurrentAmount() const noexcept { return amountScale; }
    const MoodVoice& getCurrentVoice() const noexcept { return voice; }

    static constexpr int controlBlockSize = 32;

private:
    void updateMoodTargets() noexcept;

    double sampleRate = 44100.0;
    Controls controls;

    juce::SmoothedValue<float> smoothX, smoothY, smoothIntensity, smoothMix, smoothGain;

    MoodWeights weights { 0.25f, 0.25f, 0.25f, 0.25f };
    MoodVoice voice;
    float amountScale = 0.0f;

    SaturationStage saturation;
    FilterStage filter;
    ModDelayStage modDelay;
    TremoloStage tremolo;
    PitchStage pitch;
    EchoStage echo;
    ReverbStage reverb;
    CompressorStage compressor;

    std::array<float, controlBlockSize> dryL {}, dryR {};
};

} // namespace emo
