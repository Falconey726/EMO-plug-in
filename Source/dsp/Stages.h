#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>
#include <cmath>

#include "MoodMap.h"

/*  The individual effect stages. Each one takes its targets from a MoodVoice,
    smooths them per sample, and processes a short stereo block in place.

    Design rule: when every "amount" in the voice is zero, each stage must pass
    the signal through unchanged. That is what makes the centre of the mood
    field the true dry signal.
*/

namespace emo
{

using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

constexpr float twoPi = juce::MathConstants<float>::twoPi;

//==============================================================================
/** A power-of-two circular buffer with fractional (Hermite) reads. */
class DelayBuffer
{
public:
    void prepare (int maxDelaySamples)
    {
        const auto size = juce::nextPowerOfTwo (maxDelaySamples + 8);
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
        maxDelay = (float) maxDelaySamples;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Reads `delay` samples behind the most recently pushed sample (delay >= 2). */
    float read (float delay) const noexcept
    {
        delay = juce::jlimit (2.0f, maxDelay, delay);
        const auto pos = (float) (writePos - 1) - delay;
        const auto base = (int) std::floor (pos);
        const auto f = pos - (float) base;

        const auto ym1 = at (base - 1);
        const auto y0  = at (base);
        const auto y1  = at (base + 1);
        const auto y2  = at (base + 2);

        // 4-point, 3rd-order Hermite
        const auto c0 = y0;
        const auto c1 = 0.5f * (y1 - ym1);
        const auto c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const auto c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

    /** Integer read, for fixed-length allpasses. */
    float readInt (int delay) const noexcept { return at (writePos - 1 - delay); }

private:
    float at (int index) const noexcept { return buffer[(size_t) (index & mask)]; }

    std::vector<float> buffer;
    int mask = 0, writePos = 0;
    float maxDelay = 0.0f;
};

//==============================================================================
/** Soft-clip saturation (tanh), blended in by drive so drive 0 is a clean bypass. */
class SaturationStage
{
public:
    void prepare (double sr) { drive.reset (sr, 0.05); }
    void reset() { drive.setCurrentAndTargetValue (drive.getTargetValue()); }
    void setTargets (const MoodVoice& v) { drive.setTargetValue (juce::jlimit (0.0f, 1.0f, v.drive)); }
    void snap() { drive.setCurrentAndTargetValue (drive.getTargetValue()); }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto d = drive.getNextValue();
            if (d <= 0.0f)
                continue;

            const auto g = 1.0f + 10.0f * d;
            const auto norm = 1.0f / std::pow (g, 0.75f);  // rough loudness compensation
            const auto blend = std::min (1.0f, 2.0f * d);

            l[i] += blend * (std::tanh (g * l[i]) * norm - l[i]);
            r[i] += blend * (std::tanh (g * r[i]) * norm - r[i]);
        }
    }

private:
    Smoothed drive;
};

//==============================================================================
/** First-order high shelf (tilt toward bright or dark) followed by a
    resonance-free low-pass that darkens the sound. */
class FilterStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) 512, 2 };

        shelfSplit.prepare (spec);
        shelfSplit.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
        shelfSplit.setCutoffFrequency (3500.0f);

        lowpass.prepare (spec);
        lowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        lowpass.setResonance (0.6f);
        lowpass.setCutoffFrequency (maxCutoff());

        shelfDb.reset (sr, 0.05);
        darkness.reset (sr, 0.05);
    }

    void reset() { shelfSplit.reset(); lowpass.reset(); }

    void setTargets (const MoodVoice& v)
    {
        shelfDb.setTargetValue (juce::jlimit (-12.0f, 12.0f, v.shelfDb));
        darkness.setTargetValue (juce::jlimit (0.0f, 1.0f, v.darkness));
    }

    void snap()
    {
        shelfDb.setCurrentAndTargetValue (shelfDb.getTargetValue());
        darkness.setCurrentAndTargetValue (darkness.getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        // The low-pass cutoff is updated once per control block; the darkness
        // value feeding it is already smooth, so the steps are inaudible.
        const auto dark = darkness.getCurrentValue();
        lowpass.setCutoffFrequency (cutoffFor (dark));

        for (int i = 0; i < n; ++i)
        {
            const auto shelfGain = juce::Decibels::decibelsToGain (shelfDb.getNextValue()) - 1.0f;
            const auto d = darkness.getNextValue();
            const auto lpBlend = std::min (1.0f, 5.0f * d);

            float* ch[2] = { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                auto x = *ch[c];
                x += shelfGain * shelfSplit.processSample (c, x);

                const auto lp = lowpass.processSample (c, x);
                *ch[c] = x + lpBlend * (lp - x);
            }
        }
    }

private:
    float maxCutoff() const { return (float) std::min (18000.0, sampleRate * 0.45); }

    float cutoffFor (float dark) const
    {
        // darkness 0 -> 18 kHz, 1 -> 600 Hz, exponential in between
        return juce::jlimit (200.0f, maxCutoff(), 18000.0f * std::pow (600.0f / 18000.0f, dark));
    }

    double sampleRate = 44100.0;
    juce::dsp::FirstOrderTPTFilter<float> shelfSplit;
    juce::dsp::StateVariableTPTFilter<float> lowpass;
    Smoothed shelfDb, darkness;
};

//==============================================================================
/** One modulated delay that is a chorus at partial mix and a vibrato (pure
    pitch modulation) at full mix. Left and right LFOs are 90 degrees apart. */
class ModDelayStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& b : lines)
            b.prepare ((int) std::ceil (sr * 0.03));   // 30 ms is plenty
        mix.reset (sr, 0.05);
        depthMs.reset (sr, 0.05);
        rateHz.reset (sr, 0.2);
    }

    void reset() { for (auto& b : lines) b.reset(); phase = 0.0f; }

    void setTargets (const MoodVoice& v)
    {
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, v.modMix));
        depthMs.setTargetValue (juce::jlimit (0.0f, 10.0f, v.modDepthMs));
        rateHz.setTargetValue (juce::jlimit (0.05f, 12.0f, v.modRateHz));
    }

    void snap()
    {
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        depthMs.setCurrentAndTargetValue (depthMs.getTargetValue());
        rateHz.setCurrentAndTargetValue (rateHz.getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        const auto msToSamples = (float) sampleRate * 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const auto m = mix.getNextValue();
            const auto depth = depthMs.getNextValue() * msToSamples;
            phase += rateHz.getNextValue() / (float) sampleRate;
            phase -= std::floor (phase);

            lines[0].push (l[i]);
            lines[1].push (r[i]);

            if (m <= 0.0f)
                continue;

            const auto lfoL = 0.5f + 0.5f * std::sin (twoPi * phase);
            const auto lfoR = 0.5f + 0.5f * std::sin (twoPi * phase + 0.25f * twoPi);

            const auto wetL = lines[0].read (baseDelayMs * msToSamples + depth * lfoL);
            const auto wetR = lines[1].read (baseDelayMs * msToSamples + depth * lfoR);

            l[i] += m * (wetL - l[i]);
            r[i] += m * (wetR - r[i]);
        }
    }

private:
    static constexpr float baseDelayMs = 5.0f;

    double sampleRate = 44100.0;
    std::array<DelayBuffer, 2> lines;
    Smoothed mix, depthMs, rateHz;
    float phase = 0.0f;
};

//==============================================================================
/** Amplitude tremolo whose LFO morphs from a smooth sine (bouncy) to a
    near-square pulse (tense). */
class TremoloStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        depth.reset (sr, 0.05);
        rateHz.reset (sr, 0.2);
        shape.reset (sr, 0.1);
    }

    void reset() { phase = 0.0f; }

    void setTargets (const MoodVoice& v)
    {
        depth.setTargetValue (juce::jlimit (0.0f, 1.0f, v.tremDepth));
        rateHz.setTargetValue (juce::jlimit (0.1f, 20.0f, v.tremRateHz));
        shape.setTargetValue (juce::jlimit (0.0f, 1.0f, v.tremShape));
    }

    void snap()
    {
        depth.setCurrentAndTargetValue (depth.getTargetValue());
        rateHz.setCurrentAndTargetValue (rateHz.getTargetValue());
        shape.setCurrentAndTargetValue (shape.getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        static const float squareNorm = 1.0f / std::tanh (4.0f);

        for (int i = 0; i < n; ++i)
        {
            const auto d = depth.getNextValue();
            const auto s = shape.getNextValue();
            phase += rateHz.getNextValue() / (float) sampleRate;
            phase -= std::floor (phase);

            if (d <= 0.0f)
                continue;

            const auto sine = std::sin (twoPi * phase);
            const auto pulse = std::tanh (4.0f * sine) * squareNorm;
            const auto lfo = 0.5f + 0.5f * (sine + s * (pulse - sine));   // 0..1
            const auto gain = 1.0f - d * (1.0f - lfo);

            l[i] *= gain;
            r[i] *= gain;
        }
    }

private:
    double sampleRate = 44100.0;
    Smoothed depth, rateHz, shape;
    float phase = 0.0f;
};

//==============================================================================
/** Classic two-tap delay-line pitch shifter with Hann crossfades. Cheap and
    zero-latency to set up; good for octave shimmer and small detunes. */
class PitchShifter
{
public:
    void prepare (double sr, float windowMs)
    {
        window = (float) sr * windowMs * 0.001f;
        for (auto& b : lines)
            b.prepare ((int) std::ceil (window) + 8);
        phase = 0.0f;
    }

    void reset() { for (auto& b : lines) b.reset(); phase = 0.0f; }

    void setRatio (float r) noexcept { ratio = r; }

    void process (float& l, float& r) noexcept
    {
        lines[0].push (l);
        lines[1].push (r);

        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);

        const auto phase2 = phase + 0.5f - std::floor (phase + 0.5f);
        const auto d1 = 2.0f + phase * window;
        const auto d2 = 2.0f + phase2 * window;

        const auto s = std::sin (juce::MathConstants<float>::pi * phase);
        const auto g1 = s * s;
        const auto g2 = 1.0f - g1;

        l = g1 * lines[0].read (d1) + g2 * lines[0].read (d2);
        r = g1 * lines[1].read (d1) + g2 * lines[1].read (d2);
    }

private:
    std::array<DelayBuffer, 2> lines;
    float window = 1000.0f, phase = 0.0f, ratio = 1.0f;
};

/** Pitch stage: a slight downward sag (in series) and an octave-up shimmer
    layered on top. */
class PitchStage
{
public:
    void prepare (double sr)
    {
        sag.prepare (sr, 60.0f);
        octave.prepare (sr, 35.0f);
        octave.setRatio (2.0f);
        shimmerOctaveHighpass.prepare ({ sr, 512, 2 });
        shimmerOctaveHighpass.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
        shimmerOctaveHighpass.setCutoffFrequency (400.0f);
        shimmerMix.reset (sr, 0.05);
        sagCents.reset (sr, 0.1);
    }

    void reset() { sag.reset(); octave.reset(); shimmerOctaveHighpass.reset(); }

    void setTargets (const MoodVoice& v)
    {
        shimmerMix.setTargetValue (juce::jlimit (0.0f, 1.0f, v.shimmerMix));
        sagCents.setTargetValue (juce::jlimit (-100.0f, 100.0f, v.sagCents));
    }

    void snap()
    {
        shimmerMix.setCurrentAndTargetValue (shimmerMix.getTargetValue());
        sagCents.setCurrentAndTargetValue (sagCents.getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto cents = sagCents.getNextValue();
            const auto shimmer = shimmerMix.getNextValue();

            // Sag: crossfade in the detuned copy as the sag deepens, so the
            // neutral centre never passes through the shifter at all.
            {
                auto sl = l[i], sr = r[i];
                sag.setRatio (std::pow (2.0f, cents / 1200.0f));
                sag.process (sl, sr);

                const auto blend = std::min (1.0f, std::abs (cents) / 3.0f);
                l[i] += blend * (sl - l[i]);
                r[i] += blend * (sr - r[i]);
            }

            // Shimmer: octave-up copy, thinned below 400 Hz, layered on top.
            {
                auto ol = l[i], orr = r[i];
                octave.process (ol, orr);
                ol = shimmerOctaveHighpass.processSample (0, ol);
                orr = shimmerOctaveHighpass.processSample (1, orr);
                l[i] += shimmer * ol;
                r[i] += shimmer * orr;
            }
        }
    }

private:
    PitchShifter sag, octave;
    juce::dsp::FirstOrderTPTFilter<float> shimmerOctaveHighpass;
    Smoothed shimmerMix, sagCents;
};

//==============================================================================
/** Stereo / ping-pong echo. "Smear" darkens the repeats and runs them through
    an allpass so each echo is a little more blurred than the last. */
class EchoStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& b : lines)
            b.prepare ((int) std::ceil (sr * 2.0));
        allpassL.prepare ((int) (sr * 0.02));
        allpassR.prepare ((int) (sr * 0.02));
        apLenL = (int) (sr * 0.0113);
        apLenR = (int) (sr * 0.0137);

        mix.reset (sr, 0.05);
        timeSamples.reset (sr, 0.3);
        feedback.reset (sr, 0.05);
        pingPong.reset (sr, 0.1);
        smear.reset (sr, 0.1);
    }

    void reset()
    {
        for (auto& b : lines) b.reset();
        allpassL.reset(); allpassR.reset();
        lpL = lpR = 0.0f;
        lastL = lastR = 0.0f;
    }

    void setTargets (const MoodVoice& v)
    {
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, v.delayMix));
        timeSamples.setTargetValue (juce::jlimit (5.0f, 1900.0f, v.delayTimeMs) * 0.001f * (float) sampleRate);
        feedback.setTargetValue (juce::jlimit (0.0f, 0.9f, v.delayFeedback));
        pingPong.setTargetValue (juce::jlimit (0.0f, 1.0f, v.delayPingPong));
        smear.setTargetValue (juce::jlimit (0.0f, 1.0f, v.delaySmear));
    }

    void snap()
    {
        for (auto* s : { &mix, &timeSamples, &feedback, &pingPong, &smear })
            s->setCurrentAndTargetValue (s->getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto m = mix.getNextValue();
            const auto t = timeSamples.getNextValue();
            const auto fb = feedback.getNextValue();
            const auto pp = pingPong.getNextValue();
            const auto sm = smear.getNextValue();

            const auto inL = l[i], inR = r[i];
            const auto mono = 0.5f * (inL + inR);

            // Feedback path: darken, then diffuse.
            const auto cutoff = 16000.0f + sm * (2500.0f - 16000.0f);
            const auto a = std::exp (-twoPi * cutoff / (float) sampleRate);
            lpL = lastL + a * (lpL - lastL);
            lpR = lastR + a * (lpR - lastR);

            const auto g = 0.65f * sm;
            const auto fbL = allpass (allpassL, apLenL, lpL, g);
            const auto fbR = allpass (allpassR, apLenR, lpR, g);

            // Ping-pong: mono input enters on the left and repeats cross sides.
            const auto writeL = inL + pp * (mono - inL) + fb * (fbL + pp * (fbR - fbL));
            const auto writeR = inR + pp * (0.0f - inR) + fb * (fbR + pp * (fbL - fbR));

            lines[0].push (writeL);
            lines[1].push (writeR);

            lastL = lines[0].read (t);
            lastR = lines[1].read (t);

            l[i] = inL + m * lastL;
            r[i] = inR + m * lastR;
        }
    }

private:
    static float allpass (DelayBuffer& buf, int length, float x, float g) noexcept
    {
        const auto delayed = buf.readInt (length - 1);
        const auto w = x + g * delayed;
        buf.push (w);
        return delayed - g * w;
    }

    double sampleRate = 44100.0;
    std::array<DelayBuffer, 2> lines;
    DelayBuffer allpassL, allpassR;
    int apLenL = 500, apLenR = 600;
    float lpL = 0.0f, lpR = 0.0f, lastL = 0.0f, lastR = 0.0f;
    Smoothed mix, timeSamples, feedback, pingPong, smear;
};

//==============================================================================
/** Pre-delay into a Freeverb-style room (juce::Reverb), added on top of the dry signal. */
class ReverbStage
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        reverb.setSampleRate (sr);
        for (auto& b : preDelay)
            b.prepare ((int) std::ceil (sr * 0.15));
        wet.reset (sr, 0.05);
        preDelaySamples.reset (sr, 0.2);
    }

    void reset()
    {
        reverb.reset();
        for (auto& b : preDelay) b.reset();
    }

    void setTargets (const MoodVoice& v)
    {
        wet.setTargetValue (juce::jlimit (0.0f, 1.0f, v.reverbWet));
        preDelaySamples.setTargetValue (juce::jlimit (0.0f, 120.0f, v.reverbPreDelayMs) * 0.001f * (float) sampleRate);

        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.0f, 1.0f, v.reverbSize);
        p.damping = juce::jlimit (0.0f, 1.0f, v.reverbDamping);
        p.width = juce::jlimit (0.0f, 1.0f, v.reverbWidth);
        p.wetLevel = 0.5f;
        p.dryLevel = 0.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);   // juce::Reverb smooths these internally
    }

    void snap()
    {
        wet.setCurrentAndTargetValue (wet.getTargetValue());
        preDelaySamples.setCurrentAndTargetValue (preDelaySamples.getTargetValue());
    }

    void process (float* l, float* r, int n) noexcept
    {
        jassert (n <= (int) bufL.size());

        for (int i = 0; i < n; ++i)
        {
            preDelay[0].push (l[i]);
            preDelay[1].push (r[i]);
            const auto d = preDelaySamples.getNextValue();
            bufL[(size_t) i] = d < 2.0f ? l[i] : preDelay[0].read (d);
            bufR[(size_t) i] = d < 2.0f ? r[i] : preDelay[1].read (d);
        }

        reverb.processStereo (bufL.data(), bufR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const auto w = wet.getNextValue();
            l[i] += w * bufL[(size_t) i];
            r[i] += w * bufR[(size_t) i];
        }
    }

private:
    double sampleRate = 44100.0;
    juce::Reverb reverb;
    std::array<DelayBuffer, 2> preDelay;
    std::array<float, 64> bufL {}, bufR {};
    Smoothed wet, preDelaySamples;
};

//==============================================================================
/** Tight, punchy stereo-linked feed-forward compressor. The single "amount"
    sets threshold, ratio and make-up together; amount 0 is unity gain. */
class CompressorStage
{
public:
    void prepare (double sr)
    {
        attackCoeff = std::exp (-1.0f / (0.003f * (float) sr));
        releaseCoeff = std::exp (-1.0f / (0.090f * (float) sr));
        amount.reset (sr, 0.05);
        envelope = 0.0f;
    }

    void reset() { envelope = 0.0f; gainReductionDb = 0.0f; }

    void setTargets (const MoodVoice& v) { amount.setTargetValue (juce::jlimit (0.0f, 1.0f, v.compAmount)); }
    void snap() { amount.setCurrentAndTargetValue (amount.getTargetValue()); }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto c = amount.getNextValue();

            const auto peak = std::max (std::abs (l[i]), std::abs (r[i]));
            const auto coeff = peak > envelope ? attackCoeff : releaseCoeff;
            envelope = peak + coeff * (envelope - peak);

            if (c <= 0.0f)
                continue;

            const auto thresholdDb = -22.0f * c;
            const auto ratio = 1.0f + 3.0f * c;
            const auto makeupDb = 5.0f * c;

            const auto levelDb = juce::Decibels::gainToDecibels (envelope, -120.0f);
            const auto over = levelDb - thresholdDb;
            gainReductionDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;

            const auto gain = juce::Decibels::decibelsToGain (gainReductionDb + makeupDb);
            l[i] *= gain;
            r[i] *= gain;
        }
    }

    float getGainReductionDb() const noexcept { return gainReductionDb; }

private:
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, envelope = 0.0f, gainReductionDb = 0.0f;
    Smoothed amount;
};

} // namespace emo
