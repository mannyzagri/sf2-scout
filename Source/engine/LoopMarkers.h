// Loop-marker hit-test and drag math for the waveform editor, JUCE-free so the
// harness can pin it ([markers]). Conventions: the START line sits AT loopStart,
// the END line sits AFTER the last included sample (loopEnd + 1) -- so the two
// lines bracket exactly the frames the loop plays.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace sf2scout
{

enum class Marker : int { None = 0, Start = 1, End = 2 };

// Which marker a mouse at pixel `x` addresses. Non-strict: always the nearer
// line (click-to-place). Strict: only when within `grabPx` (hover hint / grab).
inline Marker pickMarker (double xStart, double xEnd, double x, double grabPx, bool strict)
{
    const double ds = std::fabs (x - xStart), de = std::fabs (x - xEnd);
    if (strict && std::min (ds, de) > grabPx) return Marker::None;
    return ds <= de ? Marker::Start : Marker::End;
}

// Moves marker `m` to the frame under the mouse. Never crosses the other
// marker, never leaves the file; `lastFrame` = frames - 1.
inline void applyMarkerDrag (Marker m, int64_t frame, int64_t& start, int64_t& end, int64_t lastFrame)
{
    if (lastFrame < 0) return;
    frame = std::max<int64_t> (0, std::min (frame, lastFrame + 1));
    if (m == Marker::Start)      start = std::min (frame, end);
    else if (m == Marker::End)   end   = std::max (start, std::min (frame - 1, lastFrame));
}

} // namespace sf2scout
