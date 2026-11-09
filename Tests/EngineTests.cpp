// Host-free checks for the mood math and the DSP engine.
// Run: ctest --output-on-failure   (or the EmotionMorphTests binary directly)

#include "dsp/MoodEngine.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "pass" : "FAIL", what);
    if (! ok)
        ++failures;
}

constexpr double sampleRate = 48000.0;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (size_t n) : l (n, 0.0f), r (n, 0.0f) {}
};

Stereo noise (size_t n, float amp, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-amp, amp);
    Stereo s (n);
    for (size_t i = 0; i < n; ++i) { s.l[i] = d (rng); s.r[i] = d (rng); }
    return s;
}

Stereo run (Stereo s, emo::MoodEngine::Controls c, int blockSize = 512)
{
    emo::MoodEngine engine;
    engine.setControls (c);
    engine.prepare (sampleRate, blockSize);

    for (size_t start = 0; start < s.l.size(); start += (size_t) blockSize)
    {
        const auto n = (int) std::min ((size_t) blockSize, s.l.size() - start);
        engine.process (s.l.data() + start, s.r.data() + start, n);
    }
    return s;
}

double energy (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    const auto end = std::min (to, x.size());
    double e = 0.0;
    for (auto it = x.begin() + (std::ptrdiff_t) std::min (from, end); it != x.begin() + (std::ptrdiff_t) end; ++it)
        e += (double) *it * *it;
    return e;
}

/** Share of energy above roughly a few kHz, using a first difference as a crude high-pass. */
double brightness (const std::vector<float>& x)
{
    double hi = 0.0;
    for (size_t i = 1; i < x.size(); ++i)
        hi += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return hi / std::max (1.0e-12, energy (x));
}

bool allFinite (const Stereo& s)
{
    for (size_t i = 0; i < s.l.size(); ++i)
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
    return true;
}

float peak (const Stereo& s)
{
    float p = 0.0f;
    for (size_t i = 0; i < s.l.size(); ++i)
        p = std::max (p, std::max (std::abs (s.l[i]), std::abs (s.r[i])));
    return p;
}

bool isOff (float v) { return std::abs (v) < 1.0e-9f; }

emo::MoodEngine::Controls at (float x, float y) { return { x, y, 1.0f, 1.0f, 0.0f }; }
} // namespace

int main()
{
    using namespace emo;

    // --- Mood weights -------------------------------------------------------
    {
        bool sumsToOne = true;
        for (float x = -1.2f; x <= 1.2f; x += 0.1f)
            for (float y = -1.2f; y <= 1.2f; y += 0.1f)
            {
                const auto w = computeMoodWeights ({ x, y });
                float s = 0.0f;
                for (auto v : w) { s += v; sumsToOne &= (v >= 0.0f); }
                sumsToOne &= std::abs (s - 1.0f) < 1.0e-4f;
            }
        check (sumsToOne, "weights are non-negative and sum to 1 everywhere");

        const auto c = computeMoodWeights ({ 0.0f, 0.0f });
        check (std::abs (c[0] - 0.25f) < 1e-5f && std::abs (c[3] - 0.25f) < 1e-5f, "centre weights are equal");

        bool anchorsDominate = true;
        for (int i = 0; i < numMoods; ++i)
            anchorsDominate &= computeMoodWeights (moodAnchors[(size_t) i])[(size_t) i] > 0.99f;
        check (anchorsDominate, "each mood dominates at its own anchor");

        const auto halfJoy = computeMoodWeights ({ 0.0f, 0.5f });
        check (halfJoy[(size_t) Mood::Joy] > 0.7f, "halfway to Joy is mostly Joy");

        check (std::abs (distanceFromCentre ({ 3.0f, 4.0f }) - 1.0f) < 1.0e-6f, "positions outside the circle are clamped");
    }

    // --- Blending -----------------------------------------------------------
    {
        const auto v = blendMoods (computeMoodWeights ({ 0.3f, -0.4f }), 0.0f);
        check (isOff (v.drive) && isOff (v.shelfDb) && isOff (v.darkness) && isOff (v.modMix)
                   && isOff (v.tremDepth) && isOff (v.shimmerMix) && isOff (v.sagCents)
                   && isOff (v.delayMix) && isOff (v.reverbWet) && isOff (v.compAmount),
               "zero amount means every stage is off");

        const auto nearTension = blendMoods (computeMoodWeights ({ -0.9f, 0.1f }), 0.9f);
        check (std::abs (nearTension.tremRateHz - 8.0f) < 1.0f,
               "tremolo rate near Tension follows Tension, not the moods without tremolo");
    }

    const size_t len = (size_t) (sampleRate * 3.0);

    // --- Neutral centre is the dry signal ------------------------------------
    {
        const auto in = noise (len, 0.5f);
        const auto out = run (in, at (0.0f, 0.0f));
        float maxErr = 0.0f;
        for (size_t i = 0; i < len; ++i)
            maxErr = std::max (maxErr, std::max (std::abs (out.l[i] - in.l[i]), std::abs (out.r[i] - in.r[i])));
        check (maxErr < 1.0e-6f, "centre position passes the signal through untouched");

        const auto zeroIntensity = run (in, { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f });
        maxErr = 0.0f;
        for (size_t i = 0; i < len; ++i)
            maxErr = std::max (maxErr, std::abs (zeroIntensity.l[i] - in.l[i]));
        check (maxErr < 1.0e-6f, "intensity 0 is dry even at a corner");

        const auto dryMix = run (in, { -1.0f, 0.0f, 1.0f, 0.0f, 0.0f });
        maxErr = 0.0f;
        for (size_t i = 0; i < len; ++i)
            maxErr = std::max (maxErr, std::abs (dryMix.l[i] - in.l[i]));
        check (maxErr < 1.0e-6f, "mix 0 is dry");
    }

    // --- Stability everywhere on the field -----------------------------------
    {
        bool ok = true;
        float worstPeak = 0.0f;
        for (float x = -1.0f; x <= 1.01f; x += 0.25f)
            for (float y = -1.0f; y <= 1.01f; y += 0.25f)
            {
                const auto out = run (noise (len / 2, 0.5f), at (x, y), 256);
                ok &= allFinite (out);
                worstPeak = std::max (worstPeak, peak (out));
            }
        check (ok, "output is finite at every position");
        std::printf ("      worst peak across the field: %.2f (input peak 0.5)\n", worstPeak);
        check (worstPeak < 4.0f, "no runaway gain anywhere on the field");
    }

    // --- Fast automation sweep ----------------------------------------------
    {
        emo::MoodEngine engine;
        engine.prepare (sampleRate, 64);
        auto s = noise (len, 0.3f, 7);
        for (size_t start = 0, block = 0; start < len; start += 64, ++block)
        {
            const auto angle = (float) block * 0.05f;
            engine.setControls ({ std::cos (angle), std::sin (angle), 1.0f, 1.0f, 0.0f });
            engine.process (s.l.data() + start, s.r.data() + start, 64);
        }
        check (allFinite (s) && peak (s) < 4.0f, "fast circular automation stays stable");
    }

    // --- Each mood does what it says ----------------------------------------
    {
        const auto in = noise (len, 0.25f, 3);
        const auto dryBright = brightness (in.l);

        const auto joy = run (in, at (0.0f, 1.0f));
        const auto mel = run (in, at (0.0f, -1.0f));
        std::printf ("      brightness dry %.3f, joy %.3f, melancholy %.3f\n",
                     dryBright, brightness (joy.l), brightness (mel.l));
        check (brightness (joy.l) > dryBright, "Joy is brighter than dry");
        check (brightness (mel.l) < 0.5 * dryBright, "Melancholy is much darker than dry");

        // Tension compresses: a quiet passage followed by a loud one should end
        // up closer in level than in the input.
        Stereo dyn (len);
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        for (size_t i = 0; i < len; ++i)
        {
            const auto amp = i < len / 2 ? 0.05f : 0.5f;
            dyn.l[i] = dyn.r[i] = amp * std::sin ((float) i * 0.03f) + 0.01f * d (rng);
        }
        const auto ten = run (dyn, at (-1.0f, 0.0f));
        const auto inRatio = energy (dyn.l, len / 2 + 4800) / energy (dyn.l, 4800, len / 2);
        const auto outRatio = energy (ten.l, len / 2 + 4800) / energy (ten.l, 4800, len / 2);
        std::printf ("      loud/quiet energy ratio dry %.1f, tension %.1f\n", inRatio, outRatio);
        check (outRatio < 0.5 * inRatio, "Tension squashes dynamics");

        // Dream echoes: an impulse should come back around 380 ms (ping-pong starts on the left).
        Stereo imp (len);
        imp.l[100] = imp.r[100] = 1.0f;
        const auto dream = run (imp, at (1.0f, 0.0f));
        const auto echoAt = (size_t) (0.380 * sampleRate) + 100;
        const auto win = (size_t) (0.02 * sampleRate);
        const auto e1L = energy (dream.l, echoAt - win, echoAt + win);
        const auto e1R = energy (dream.r, echoAt - win, echoAt + win);
        const auto e2L = energy (dream.l, 2 * echoAt - 100 - win, 2 * echoAt - 100 + win);
        std::printf ("      dream echo energy: 1st L %.4f R %.4f, 2nd L %.4f\n", e1L, e1R, e2L);
        check (e1L > 1.0e-3 || e1R > 1.0e-3, "Dream produces an echo near 380 ms");
        check (e2L > 1.0e-4, "Dream echoes repeat");
    }

    std::printf (failures == 0 ? "\nAll tests passed.\n" : "\n%d test(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
