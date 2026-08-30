// Note-name + millisecond formatting shared by the readout and the harness.
// Matches the handoff mock's `nn()` and `fmt()` exactly (docs/handoff-gui-v1/README.md).
#pragma once
#include <string>
#include <cstdio>

namespace sf2scout
{
inline std::string noteName (int n)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    if (n < 0 || n > 127) return "\xE2\x80\x94";   // em dash
    const int octave = (n / 12) - 1;
    return std::string (names[n % 12]) + std::to_string (octave);
}

inline bool isBlackKey (int n)
{
    const int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

// "1 decimal below 100 ms, 0 decimals at or above"
inline std::string formatMs (double ms)
{
    char buf[48];
    if (ms < 100.0) std::snprintf (buf, sizeof (buf), "%.1f ms", ms);
    else            std::snprintf (buf, sizeof (buf), "%.0f ms", ms);
    return buf;
}

inline double samplesToMs (double samples, double rate)
{
    return rate > 0.0 ? samples / rate * 1000.0 : 0.0;
}
}
