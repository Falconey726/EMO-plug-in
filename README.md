# Emotion Morph

A VST3 / AU audio effect that steers the emotional character of a sound with one
gesture: drag an orb across a circular mood field.

```
                 JOY
                  ▲
   TENSION ◀──  (dry)  ──▶ DREAM
                  ▼
              MELANCHOLY
```

| Mood | Where | What it does |
| --- | --- | --- |
| Joy | top | bright high shelf, octave-up shimmer, quick bouncy tremolo, short airy room |
| Melancholy | bottom | darkening low-pass, slow wide vibrato, long dark reverb with pre-delay, slight pitch sag |
| Tension | left | soft-clip saturation, pulsing tremolo, subtle pitch wobble, tight compression |
| Dream | right | lush chorus, smeared ping-pong echoes, diffused reverb, gentle high-cut |

The centre of the circle is the untouched dry signal.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 8 is downloaded automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On macOS this builds AU, VST3 and a Standalone app (universal arm64 + x86_64); on
Linux it builds VST3. To use an existing JUCE checkout instead of downloading
one, add `-DJUCE_DIR=/path/to/JUCE`. Plugins land in
`build/EmotionMorph_artefacts/Release/`. Xcode users can generate a project
with `cmake -B build -G Xcode`.

## Parameters (automatable)

| Parameter | Range | Default |
| --- | --- | --- |
| Mood X | -1 (Tension) to +1 (Dream) | 0 |
| Mood Y | -1 (Melancholy) to +1 (Joy) | 0 |
| Intensity | 0–100 % | 100 % |
| Mix | 0–100 % | 100 % |
| Output Gain | -24 to +12 dB | 0 dB |

## How the engine works

Signal chain:

```
Input → Saturation → Filter (shelf + low-pass) → Chorus/Vibrato → Tremolo
      → Pitch (sag + octave shimmer) → Delay → Reverb → Compressor → Mix → Output
```

1. **Mood weights.** The orb position (clamped to the unit circle) gets an
   inverse-distance weight for each mood corner, normalised to sum to 1. At the
   centre all four are 0.25; on a corner that mood is ~1. See
   `Source/dsp/MoodMap.h`.
2. **Blending.** Each mood is a `MoodVoice`: the full-strength settings for every
   stage. Settings are either *amounts* (drive, wet levels, depths) or
   *character* (rates, times, room size).
   - Amounts are the weighted sum of the moods, scaled by distance from centre ×
     Intensity. At the centre every amount is zero, and every stage is written
     so that zero is an exact bypass, which is what makes the centre truly dry.
   - Character values are blended only among the moods that use that stage, so
     moving toward Joy doesn't pull its tremolo rate toward Tension's.
3. **Smoothing.** The host parameters are smoothed per sample. Every 32 samples
   the smoothed orb position is turned into a fresh blend, and each stage
   smooths its own targets per sample again, so automation and fast drags don't
   zipper.

Tune the moods by editing the values in `moodVoices()` in `MoodMap.h`.

## Layout

```
Source/
  PluginProcessor.*   parameters, state, metering for the UI
  PluginEditor.*      placeholder mood pad + sliders (the orb UI comes next)
  dsp/MoodMap.h       mood anchors, weights, per-mood settings, blending
  dsp/Stages.h        the individual effect stages
  dsp/MoodEngine.*    the full chain and control-rate updates
Tests/EngineTests.cpp host-free checks of the math and the sound of each mood
```

## Status

The audio engine is in place. The editor is a functional placeholder (drag the
dot, double-click to recentre). Next up: the glowing orb UI, then mood
automation recording, custom moods and feeling-named presets.
