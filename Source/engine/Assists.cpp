#include "Assists.h"
#include <algorithm>
#include <complex>
#include <vector>

namespace sf2scout
{

namespace
{
    // in-place iterative radix-2 FFT (n must be a power of two)
    void fft (std::vector<std::complex<float>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const float ang = -2.0f * 3.14159265358979f / (float) len;
            const std::complex<float> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    // normalised magnitude spectrum (Hann window) of `n` mono frames starting at `from` (zero-padded past the file)
    std::vector<float> spectrum (const WavSample& w, int64_t from, size_t n)
    {
        std::vector<std::complex<float>> a (n);
        for (size_t i = 0; i < n; ++i)
        {
            const int64_t f = from + (int64_t) i;
            const float v = (f >= 0 && f < (int64_t) w.frames) ? monoAt (w, (uint32_t) f) : 0.0f;
            const float win = 0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f * (float) i / (float) (n - 1));
            a[i] = std::complex<float> (v * win, 0.0f);
        }
        fft (a);
        std::vector<float> mag (n / 2);
        double norm = 0.0;
        for (size_t i = 0; i < n / 2; ++i) { mag[i] = std::abs (a[i]); norm += (double) mag[i] * mag[i]; }
        const float inv = norm > 0.0 ? (float) (1.0 / std::sqrt (norm)) : 0.0f;
        for (auto& m : mag) m *= inv;
        return mag;
    }

    double rmsOf (const WavSample& w, int64_t from, int64_t to)   // [from, to)
    {
        from = std::max<int64_t> (0, from); to = std::min<int64_t> ((int64_t) w.frames, to);
        if (to <= from) return 0.0;
        double acc = 0.0;
        for (int64_t i = from; i < to; ++i) { const double v = monoAt (w, (uint32_t) i); acc += v * v; }
        return std::sqrt (acc / (double) (to - from));
    }
}

double loopRmsDb (const WavSample& w, uint32_t loopStart, uint32_t loopEnd)
{
    if (w.frames == 0 || loopEnd < loopStart || loopEnd >= w.frames) return -120.0;
    const double rms = rmsOf (w, loopStart, (int64_t) loopEnd + 1);
    return rms > 1e-6 ? std::max (-120.0, 20.0 * std::log10 (rms)) : -120.0;
}

double loopPeriods (uint32_t loopLength, double sampleRate, int rootKey, double fineCents)
{
    const double hz = noteHz ((double) rootKey + fineCents / 100.0);
    if (! (sampleRate > 0.0) || ! (hz > 0.0)) return 0.0;
    return (double) loopLength * hz / sampleRate;
}

double clickRatio (const WavSample& w, uint32_t loopStart, uint32_t loopEnd, LoopKind kind)
{
    if (w.frames == 0 || loopEnd < loopStart || loopEnd >= w.frames) return 0.0;
    const uint32_t n = loopEnd - loopStart + 1;
    if (n < 4 || kind == LoopKind::Off) return 0.0;
    // render 4 passes of the loop exactly as the player sequences it (unity rate)
    std::vector<float> seq;
    std::vector<size_t> seams;
    seq.reserve ((size_t) n * 4);
    for (int pass = 0; pass < 4; ++pass)
    {
        if (kind == LoopKind::Forward || (pass % 2) == 0)
        {
            if (pass > 0) seams.push_back (seq.size() - 1);               // wrap / reflection at loopStart
            for (uint32_t i = loopStart; i <= loopEnd; ++i) seq.push_back (monoAt (w, i));
        }
        else
        {
            seams.push_back (seq.size() - 1);                             // reflection at loopEnd
            for (uint32_t i = loopEnd - 1; i > loopStart; --i) seq.push_back (monoAt (w, i));
        }
    }
    if (seq.size() < 8) return 0.0;
    std::vector<float> d (seq.size() - 1);
    for (size_t i = 0; i + 1 < seq.size(); ++i) d[i] = std::fabs (seq[i + 1] - seq[i]);
    float seam = 0.0f;
    for (size_t s : seams)
        for (size_t k = (s >= 1 ? s - 1 : s); k <= s + 1 && k < d.size(); ++k) seam = std::max (seam, d[k]);
    // reference: the 99.5th percentile of the derivative away from the seams
    std::vector<float> body;
    body.reserve (d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        bool nearSeam = false;
        for (size_t s : seams) if (i + 1 >= s && i <= s + 1) { nearSeam = true; break; }
        if (! nearSeam) body.push_back (d[i]);
    }
    if (body.empty()) return 0.0;
    const size_t idx = (size_t) std::floor (0.995 * (double) (body.size() - 1));
    std::nth_element (body.begin(), body.begin() + (std::ptrdiff_t) idx, body.end());
    const float ref = std::max (1e-4f, body[idx]);
    return (double) seam / (double) ref;
}

SpliceSuggestion suggestSplice (const WavSample& w, uint32_t loopStart, uint32_t loopEnd, double windowMs)
{
    SpliceSuggestion r;
    if (w.frames < 8 || loopStart >= w.frames - 2 || loopEnd <= loopStart) return r;
    const int64_t win = std::max<int64_t> (8, (int64_t) std::llround (windowMs * 0.001 * (double) w.sampleRate));
    const int64_t lo = std::max<int64_t> ((int64_t) loopStart + 16, (int64_t) loopEnd - win);
    const int64_t hi = std::min<int64_t> ((int64_t) w.frames - 2, (int64_t) loopEnd + win);
    if (hi <= lo) return r;
    const float xs  = monoAt (w, loopStart);
    const float xs1 = monoAt (w, std::min (loopStart + 1, w.frames - 1));
    const float slopeS = xs1 - xs;
    struct Cand { int64_t e; double cheap; };
    std::vector<Cand> cands;
    cands.reserve ((size_t) (hi - lo + 1));
    for (int64_t e = lo; e <= hi; ++e)
    {
        const float xe = monoAt (w, (uint32_t) e), xe1 = monoAt (w, (uint32_t) (e - 1));
        const float slopeE = xe - xe1;
        // after x[e] the player reads x[s]: value continuity + slope continuity
        const double amp = std::fabs ((double) xs - ((double) xe + (double) slopeE));
        const double slope = std::fabs ((double) slopeS - (double) slopeE);
        cands.push_back ({ e, amp * 3.0 + slope * 6.0 });
    }
    r.candidates = (int) cands.size();
    const size_t keep = std::min<size_t> (cands.size(), 64);
    std::partial_sort (cands.begin(), cands.begin() + (std::ptrdiff_t) keep, cands.end(), [] (const Cand& a, const Cand& b) { return a.cheap < b.cheap; });
    // spectral + RMS terms on the short list (2048-point windows either side of the seam)
    const size_t N = 2048;
    const std::vector<float> specS = spectrum (w, (int64_t) loopStart, N);
    const double rmsS = rmsOf (w, loopStart, (int64_t) loopStart + (int64_t) N);
    double best = 1e300; int64_t bestE = -1;
    for (size_t i = 0; i < keep; ++i)
    {
        const int64_t e = cands[i].e;
        const std::vector<float> specE = spectrum (w, e + 1 - (int64_t) N, N);
        double dot = 0.0;
        for (size_t k = 0; k < specS.size(); ++k) dot += (double) specS[k] * specE[k];
        const double spec = 1.0 - std::max (0.0, std::min (1.0, dot));          // cosine distance 0..1
        const double rmsE = rmsOf (w, e + 1 - (int64_t) N, e + 1);
        const double rms = std::fabs (rmsS - rmsE) / std::max (1e-4, std::max (rmsS, rmsE));
        const double score = cands[i].cheap + spec * 1.0 + rms * 2.0;
        if (score < best) { best = score; bestE = e; }
    }
    if (bestE < 0) return r;
    r.found = true;
    r.loopEnd = (uint32_t) bestE;
    r.score = best;
    return r;
}

RootEstimate autoDetectRoot (const WavSample& w, uint32_t loopStart, uint32_t loopEnd)
{
    RootEstimate r;
    if (w.frames < 64) return r;
    const double rate = (double) w.sampleRate;
    const int64_t minLag = std::max<int64_t> (2, (int64_t) std::llround (rate / 4000.0));   // 4 kHz ceiling
    const int64_t maxLag = std::max<int64_t> (minLag + 2, (int64_t) std::llround (rate / 30.0)); // 30 Hz floor
    // analysis window: the loop region, at least 4 x the longest period, at most 32768 frames
    int64_t from = std::min<int64_t> ((int64_t) loopStart, (int64_t) w.frames - 1);
    int64_t avail = (loopEnd >= loopStart && loopEnd < w.frames) ? (int64_t) loopEnd - (int64_t) loopStart + 1 : (int64_t) w.frames;
    int64_t N = std::max<int64_t> (avail, 4 * maxLag);
    if (from + N > (int64_t) w.frames) N = (int64_t) w.frames - from;
    N = std::min<int64_t> (N, 32768);
    if (N < 2 * minLag + 8) return r;
    std::vector<double> x ((size_t) N);
    double mean = 0.0;
    for (int64_t i = 0; i < N; ++i) { x[(size_t) i] = monoAt (w, (uint32_t) (from + i)); mean += x[(size_t) i]; }
    mean /= (double) N;
    for (auto& v : x) v -= mean;
    const int64_t lagHi = std::min (maxLag, N / 2);
    std::vector<double> acf ((size_t) lagHi + 1, 0.0);
    for (int64_t lag = minLag; lag <= lagHi; ++lag)
    {
        double num = 0.0, d1 = 0.0, d2 = 0.0;
        for (int64_t i = 0; i + lag < N; ++i)
        {
            const double a = x[(size_t) i], b = x[(size_t) (i + lag)];
            num += a * b; d1 += a * a; d2 += b * b;
        }
        acf[(size_t) lag] = (d1 > 0.0 && d2 > 0.0) ? num / std::sqrt (d1 * d2) : 0.0;
    }
    double rmax = 0.0;
    for (int64_t lag = minLag; lag <= lagHi; ++lag) rmax = std::max (rmax, acf[(size_t) lag]);
    if (rmax < 0.3) return r;
    // the SMALLEST lag that is a local peak within 10 % of the global maximum (avoids octave-down errors)
    int64_t bestLag = -1;
    for (int64_t lag = minLag + 1; lag < lagHi; ++lag)
        if (acf[(size_t) lag] >= 0.9 * rmax && acf[(size_t) lag] >= acf[(size_t) lag - 1] && acf[(size_t) lag] >= acf[(size_t) lag + 1]) { bestLag = lag; break; }
    if (bestLag < 0) return r;
    // parabolic refinement
    const double y0 = acf[(size_t) bestLag - 1], y1 = acf[(size_t) bestLag], y2 = acf[(size_t) bestLag + 1];
    const double den = y0 - 2.0 * y1 + y2;
    const double delta = std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
    const double lag = (double) bestLag + std::max (-1.0, std::min (1.0, delta));
    r.hz = rate / lag;
    if (! (r.hz > 0.0)) return r;
    const double midi = 69.0 + 12.0 * std::log2 (r.hz / 440.0);
    r.note = std::max (0, std::min (127, (int) std::lround (midi)));
    r.cents = (midi - (double) r.note) * 100.0;
    r.confidence = rmax;
    r.found = true;
    return r;
}

} // namespace sf2scout
