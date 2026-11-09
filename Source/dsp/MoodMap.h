#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace emo
{

//==============================================================================
/** The four moods, in a fixed order that every array in the engine follows. */
enum class Mood : int { Joy = 0, Melancholy, Tension, Dream };
constexpr int numMoods = 4;

/** Where each mood sits on the field. X runs left to right, Y runs bottom to top.
    Joy is at the top, Melancholy at the bottom, Tension left, Dream right. */
struct Point { float x = 0.0f, y = 0.0f; };

constexpr std::array<Point, numMoods> moodAnchors {{
    {  0.0f,  1.0f },   // Joy
    {  0.0f, -1.0f },   // Melancholy
    { -1.0f,  0.0f },   // Tension
    {  1.0f,  0.0f },   // Dream
}};

using MoodWeights = std::array<float, numMoods>;

//==============================================================================
/** Every value the effect chain needs. A "voice" describes one mood at full
    strength; the blended result describes what the chain should do right now.

    Fields fall into two kinds:
      - Amounts scale with the orb's distance from centre (and Intensity). At the
        centre they are all zero, which makes every stage an exact bypass.
      - Character values (rates, times, room sizes) say *how* a stage sounds,
        not how much of it there is. They are blended only between the moods
        that actually use that stage, so a little Dream on the way to Joy does
        not drag Joy's tremolo rate toward Dream's.
*/
struct MoodVoice
{
    // Saturation
    float drive = 0.0f;               // amount, 0..1

    // Filter
    float shelfDb = 0.0f;             // amount, high-shelf gain in dB
    float darkness = 0.0f;            // amount, 0..1 (low-pass depth)

    // Chorus / vibrato (one modulated delay; mix 1 = pure vibrato)
    float modMix = 0.0f;              // amount, 0..1
    float modDepthMs = 0.0f;          // amount, ms of delay swing
    float modRateHz = 1.0f;           // character

    // Tremolo
    float tremDepth = 0.0f;           // amount, 0..1
    float tremRateHz = 5.0f;          // character
    float tremShape = 0.0f;           // character, 0 = sine, 1 = near-square pulse

    // Pitch
    float shimmerMix = 0.0f;          // amount, octave-up blend 0..1
    float sagCents = 0.0f;            // amount, constant pitch offset (negative = flat)

    // Delay
    float delayMix = 0.0f;            // amount, 0..1
    float delayTimeMs = 350.0f;       // character
    float delayFeedback = 0.0f;       // character, 0..0.9
    float delayPingPong = 0.0f;       // character, 0..1
    float delaySmear = 0.0f;          // character, 0..1 (darkening + diffusion of repeats)

    // Reverb
    float reverbWet = 0.0f;           // amount, 0..1
    float reverbSize = 0.5f;          // character
    float reverbDamping = 0.5f;       // character
    float reverbWidth = 1.0f;         // character
    float reverbPreDelayMs = 0.0f;    // character

    // Compressor
    float compAmount = 0.0f;          // amount, 0..1
};

//==============================================================================
/** Target sound of each mood at full strength. Tweak these to retune a mood. */
inline const std::array<MoodVoice, numMoods>& moodVoices()
{
    static const std::array<MoodVoice, numMoods> voices = []
    {
        std::array<MoodVoice, numMoods> v {};

        // Joy: bright shelf, octave shimmer, quick bouncy tremolo, short airy room.
        auto& joy = v[(int) Mood::Joy];
        joy.shelfDb = 6.0f;
        joy.tremDepth = 0.3f;    joy.tremRateHz = 6.5f;   joy.tremShape = 0.15f;
        joy.shimmerMix = 0.3f;
        joy.reverbWet = 0.22f;   joy.reverbSize = 0.35f;  joy.reverbDamping = 0.1f;
        joy.reverbWidth = 0.8f;  joy.reverbPreDelayMs = 8.0f;
        joy.compAmount = 0.15f;

        // Melancholy: dark low-pass, slow wide vibrato, long dark reverb with
        // pre-delay, slight pitch sag.
        auto& mel = v[(int) Mood::Melancholy];
        mel.shelfDb = -2.0f;
        mel.darkness = 0.75f;
        mel.modMix = 1.0f;       mel.modDepthMs = 3.5f;   mel.modRateHz = 0.35f;
        mel.sagCents = -20.0f;
        mel.reverbWet = 0.45f;   mel.reverbSize = 0.92f;  mel.reverbDamping = 0.85f;
        mel.reverbWidth = 0.9f;  mel.reverbPreDelayMs = 45.0f;

        // Tension: soft clip, pulsing tremolo, subtle pitch wobble, tight compression.
        auto& ten = v[(int) Mood::Tension];
        ten.drive = 0.65f;
        ten.shelfDb = 1.0f;
        ten.modMix = 1.0f;       ten.modDepthMs = 0.25f;  ten.modRateHz = 5.5f;
        ten.tremDepth = 0.5f;    ten.tremRateHz = 8.0f;   ten.tremShape = 0.85f;
        ten.reverbWet = 0.05f;   ten.reverbSize = 0.2f;   ten.reverbDamping = 0.5f;
        ten.reverbWidth = 0.5f;
        ten.compAmount = 1.0f;

        // Dream: lush chorus, smeared ping-pong echoes, diffused reverb, gentle high-cut.
        auto& dream = v[(int) Mood::Dream];
        dream.darkness = 0.3f;
        dream.modMix = 0.5f;     dream.modDepthMs = 2.5f; dream.modRateHz = 0.7f;
        dream.delayMix = 0.35f;  dream.delayTimeMs = 380.0f; dream.delayFeedback = 0.5f;
        dream.delayPingPong = 1.0f; dream.delaySmear = 0.7f;
        dream.reverbWet = 0.4f;  dream.reverbSize = 0.82f; dream.reverbDamping = 0.45f;
        dream.reverbWidth = 1.0f; dream.reverbPreDelayMs = 20.0f;

        return v;
    }();

    return voices;
}

//==============================================================================
/** Keeps the orb inside the unit circle. */
inline Point clampToCircle (Point p) noexcept
{
    const auto r = std::sqrt (p.x * p.x + p.y * p.y);
    if (r > 1.0f)
        return { p.x / r, p.y / r };
    return p;
}

/** Distance of the orb from the neutral centre, 0..1. */
inline float distanceFromCentre (Point p) noexcept
{
    p = clampToCircle (p);
    return std::min (1.0f, std::sqrt (p.x * p.x + p.y * p.y));
}

/** Inverse-distance weights for the four moods, normalised to sum to 1.
    At the centre every mood gets 0.25; on an anchor that mood gets ~1. */
inline MoodWeights computeMoodWeights (Point p) noexcept
{
    p = clampToCircle (p);

    constexpr float sharpness = 1.5f;   // higher = moods dominate closer to their corner
    constexpr float epsilon = 1.0e-4f;

    MoodWeights w {};
    float sum = 0.0f;

    for (int i = 0; i < numMoods; ++i)
    {
        const auto dx = p.x - moodAnchors[(size_t) i].x;
        const auto dy = p.y - moodAnchors[(size_t) i].y;
        const auto d2 = dx * dx + dy * dy;
        w[(size_t) i] = 1.0f / std::pow (d2 + epsilon, sharpness);
        sum += w[(size_t) i];
    }

    for (auto& wi : w)
        wi /= sum;

    return w;
}

//==============================================================================
namespace detail
{
    /** Weighted average of a character field, weighted by how much each mood uses
        the stage (its `relevance` field). Falls back to the plain weighted average
        if no mood uses the stage. */
    template <typename Field, typename Relevance>
    float blendCharacter (const MoodWeights& w, Field field, Relevance relevance) noexcept
    {
        const auto& voices = moodVoices();
        float num = 0.0f, den = 0.0f, plain = 0.0f;

        for (size_t i = 0; i < (size_t) numMoods; ++i)
        {
            const auto rel = w[i] * std::abs (voices[i].*relevance);
            num += rel * (voices[i].*field);
            den += rel;
            plain += w[i] * (voices[i].*field);
        }

        return den > 1.0e-6f ? num / den : plain;
    }

    template <typename Field>
    float blendAmount (const MoodWeights& w, Field field, float scale) noexcept
    {
        const auto& voices = moodVoices();
        float sum = 0.0f;

        for (size_t i = 0; i < (size_t) numMoods; ++i)
            sum += w[i] * (voices[i].*field);

        return sum * scale;
    }
}

/** Turns orb position + intensity into the blended settings for the whole chain.
    `amountScale` = distance from centre * Intensity, so the centre is the dry
    signal and the edge is the mood at full strength. */
inline MoodVoice blendMoods (const MoodWeights& w, float amountScale) noexcept
{
    using detail::blendAmount;
    using detail::blendCharacter;
    using V = MoodVoice;

    MoodVoice out;

    out.drive        = blendAmount (w, &V::drive, amountScale);
    out.shelfDb      = blendAmount (w, &V::shelfDb, amountScale);
    out.darkness     = blendAmount (w, &V::darkness, amountScale);

    out.modMix       = blendAmount (w, &V::modMix, amountScale);
    out.modDepthMs   = blendAmount (w, &V::modDepthMs, amountScale);
    out.modRateHz    = blendCharacter (w, &V::modRateHz, &V::modMix);

    out.tremDepth    = blendAmount (w, &V::tremDepth, amountScale);
    out.tremRateHz   = blendCharacter (w, &V::tremRateHz, &V::tremDepth);
    out.tremShape    = blendCharacter (w, &V::tremShape, &V::tremDepth);

    out.shimmerMix   = blendAmount (w, &V::shimmerMix, amountScale);
    out.sagCents     = blendAmount (w, &V::sagCents, amountScale);

    out.delayMix         = blendAmount (w, &V::delayMix, amountScale);
    out.delayTimeMs      = blendCharacter (w, &V::delayTimeMs, &V::delayMix);
    out.delayFeedback    = blendCharacter (w, &V::delayFeedback, &V::delayMix);
    out.delayPingPong    = blendCharacter (w, &V::delayPingPong, &V::delayMix);
    out.delaySmear       = blendCharacter (w, &V::delaySmear, &V::delayMix);

    out.reverbWet        = blendAmount (w, &V::reverbWet, amountScale);
    out.reverbSize       = blendCharacter (w, &V::reverbSize, &V::reverbWet);
    out.reverbDamping    = blendCharacter (w, &V::reverbDamping, &V::reverbWet);
    out.reverbWidth      = blendCharacter (w, &V::reverbWidth, &V::reverbWet);
    out.reverbPreDelayMs = blendCharacter (w, &V::reverbPreDelayMs, &V::reverbWet);

    out.compAmount   = blendAmount (w, &V::compAmount, amountScale);

    return out;
}

} // namespace emo
