// Assists -- the editor's analysis helpers (docs/SCOUT_v2_SPEC.md "Editor"):
//   LOUDNESS   RMS dB of the loop region
//   PERIODS    loop length in periods of the root pitch (warn tint when non-integer)
//   CLICK      seam derivative vs the signal's 99.5th percentile, rendered over 4 passes
//   SUGGEST    local best-splice search +-200 ms: zero-crossing + slope + 2048-pt spectral + RMS
//   AUTO-DETECT ROOT  autocorrelation on the loop region -> Hz -> note + cents
// JUCE-free and pure (message thread; the harness pins them in [assists]).
#pragma once

#include "WavSample.h"
#include "ScoutEngine.h"
#include <cstdint>
#include <cmath>

namespace sf2scout
{

// frequency of a MIDI note (+ fractional semitones) with A4 = 440
inline double noteHz (double midiNote) { return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0); }

// mono view of a frame (stereo: mean of both channels)
inline float monoAt (const WavSample& w, uint32_t i) { return w.isStereo() ? 0.5f * (w.left[i] + w.right[i]) : w.left[i]; }

// RMS of the loop region [loopStart, loopEnd] in dB (floor -120). Empty/invalid region -> -120.
double loopRmsDb (const WavSample& w, uint32_t loopStart, uint32_t loopEnd);

// loop length in periods of the root pitch at the sample's rate
double loopPeriods (uint32_t loopLength, double sampleRate, int rootKey, double fineCents);
inline bool periodsNearInteger (double periods, double tol = 0.05) { return std::fabs (periods - std::round (periods)) < tol; }

// CLICK METER ratio: < 1 green, 1..2 yellow, > 2 red
double clickRatio (const WavSample& w, uint32_t loopStart, uint32_t loopEnd, LoopKind kind);

struct SpliceSuggestion
{
    bool     found = false;
    uint32_t loopEnd = 0;          // inclusive
    double   score = 0.0;          // lower is better
    int      candidates = 0;
};
// Keeps loopStart, searches a better loopEnd within +-windowMs of the current one.
SpliceSuggestion suggestSplice (const WavSample& w, uint32_t loopStart, uint32_t loopEnd, double windowMs = 200.0);

struct RootEstimate
{
    bool   found = false;
    double hz = 0.0;
    int    note = 60;
    double cents = 0.0;            // -50..50
    double confidence = 0.0;       // normalised autocorrelation peak, 0..1
};
// Autocorrelation on the loop region (falls back to the whole file when the loop is tiny).
RootEstimate autoDetectRoot (const WavSample& w, uint32_t loopStart, uint32_t loopEnd);

} // namespace sf2scout
