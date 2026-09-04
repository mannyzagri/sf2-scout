#include "WavSample.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace sf2scout
{

namespace
{
struct Reader
{
    const uint8_t* p; size_t n;
    bool ok (size_t at, size_t len) const { return at <= n && len <= n - at; }
    uint16_t u16 (size_t at) const { return (uint16_t) (p[at] | (p[at + 1] << 8)); }
    uint32_t u32 (size_t at) const { return (uint32_t) p[at] | ((uint32_t) p[at + 1] << 8) | ((uint32_t) p[at + 2] << 16) | ((uint32_t) p[at + 3] << 24); }
};

struct Writer
{
    std::vector<uint8_t> b;
    void u8 (uint8_t v)   { b.push_back (v); }
    void u16 (uint16_t v) { u8 ((uint8_t) v); u8 ((uint8_t) (v >> 8)); }
    void u32 (uint32_t v) { u16 ((uint16_t) v); u16 ((uint16_t) (v >> 16)); }
    void fcc (const char* s) { for (int i = 0; i < 4; ++i) u8 ((uint8_t) s[i]); }
    void fixed (const std::string& s, size_t len) { for (size_t i = 0; i < len; ++i) u8 (i < s.size() ? (uint8_t) s[i] : 0); }
    void chunk (const char* id, const std::vector<uint8_t>& body)
    {
        fcc (id); u32 ((uint32_t) body.size());
        b.insert (b.end(), body.begin(), body.end());
        if (body.size() & 1) u8 (0);
    }
};

// frame decode helpers: value of channel `ch` in frame `f`
float decodeSample (const uint8_t* s, int bits, int formatTag)
{
    if (formatTag == 3 && bits == 32) { float f; std::memcpy (&f, s, 4); return f; }
    if (formatTag == 3 && bits == 64) { double d; std::memcpy (&d, s, 8); return (float) d; }
    switch (bits)
    {
        case 8:  return ((int) s[0] - 128) / 128.0f;
        case 16: return (float) (int16_t) (s[0] | (s[1] << 8)) / 32768.0f;
        case 24: { int32_t v = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8; return (float) v / 8388608.0f; }
        case 32: { int32_t v = (int32_t) ((uint32_t) s[0] | (uint32_t) s[1] << 8 | (uint32_t) s[2] << 16 | (uint32_t) s[3] << 24); return (float) ((double) v / 2147483648.0); }
        default: return 0.0f;
    }
}
} // namespace

uint32_t centsToPitchFraction (double cents)
{
    const double c = std::max (0.0, std::min (99.9999, cents));
    const double f = std::round (c / 100.0 * 4294967296.0);
    return (uint32_t) std::min (4294967295.0, f);
}

double pitchFractionToCents (uint32_t fraction)
{
    return (double) fraction / 4294967296.0 * 100.0;
}

int rootFromFileName (const std::string& fileName)
{
    std::string stem = fileName;
    const size_t dot = stem.rfind ('.');
    if (dot != std::string::npos) stem = stem.substr (0, dot);
    const size_t us = stem.rfind ('_');
    if (us == std::string::npos || us + 1 >= stem.size()) return -1;
    const std::string note = stem.substr (us + 1);
    size_t i = 0;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char l = (char) std::toupper ((unsigned char) note[i]);
    if (l < 'A' || l > 'G') return -1;
    int semi = base[l - 'A'];
    ++i;
    if (i < note.size() && note[i] == '#') { ++semi; ++i; }
    else if (i < note.size() && note[i] == 'b') { --semi; ++i; }
    if (i >= note.size()) return -1;
    bool neg = false;
    if (note[i] == '-') { neg = true; ++i; }
    if (i >= note.size()) return -1;
    int oct = 0;
    for (; i < note.size(); ++i)
    {
        if (note[i] < '0' || note[i] > '9') return -1;
        oct = oct * 10 + (note[i] - '0');
        if (oct > 9) return -1;
    }
    if (neg) oct = -oct;
    const int n = (oct + 1) * 12 + semi;
    return (n >= 0 && n <= 127) ? n : -1;
}

std::unique_ptr<WavSample> WavSample::load (const void* data, size_t size, const std::string& fileName, std::string& error)
{
    error.clear();
    if (data == nullptr || size < 12) { error = "not a WAV file (too short)"; return nullptr; }
    Reader r { (const uint8_t*) data, size };
    if (std::memcmp (r.p, "RIFF", 4) != 0 || std::memcmp (r.p + 8, "WAVE", 4) != 0) { error = "not a RIFF/WAVE file"; return nullptr; }

    auto w = std::make_unique<WavSample>();
    w->fileName = fileName;
    bool haveFmt = false, haveData = false;
    size_t dataAt = 0, dataLen = 0;
    int blockAlign = 0;

    size_t at = 12;
    while (r.ok (at, 8))
    {
        char id[4]; std::memcpy (id, r.p + at, 4);
        size_t len = r.u32 (at + 4);
        const size_t bodyAt = at + 8;
        if (! r.ok (bodyAt, len))
        {
            // a truncated final `data` chunk is tolerated (common with streaming writers); anything else is malformed
            if (std::memcmp (id, "data", 4) == 0 && bodyAt <= size) len = size - bodyAt;
            else { error = "truncated chunk '" + std::string (id, 4) + "'"; return nullptr; }
        }
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            if (len < 16) { error = "fmt chunk too short"; return nullptr; }
            w->formatTag     = r.u16 (bodyAt);
            w->channels      = r.u16 (bodyAt + 2);
            w->sampleRate    = r.u32 (bodyAt + 4);
            blockAlign       = r.u16 (bodyAt + 12);
            w->bitsPerSample = r.u16 (bodyAt + 14);
            if (w->formatTag == 0xFFFE)   // WAVE_FORMAT_EXTENSIBLE: sub-format GUID's first two bytes
            {
                if (len < 40) { error = "extensible fmt chunk too short"; return nullptr; }
                w->formatTag = r.u16 (bodyAt + 24);
            }
            haveFmt = true;
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            if (haveData) { error = "more than one data chunk"; return nullptr; }
            haveData = true; dataAt = bodyAt; dataLen = len;
        }
        else if (std::memcmp (id, "smpl", 4) == 0)
        {
            if (len >= 36)
            {
                const uint32_t unity = r.u32 (bodyAt + 12);
                const uint32_t frac  = r.u32 (bodyAt + 16);
                const uint32_t nLoops = r.u32 (bodyAt + 28);
                w->hasSmpl = true;
                if (unity <= 127) { w->rootKey = (int) unity; w->rootFromFile = true; }
                w->fineCents = pitchFractionToCents (frac);
                if (w->fineCents >= 50.0) { w->fineCents -= 100.0; w->rootKey = std::min (127, w->rootKey + 1); }
                if (nLoops >= 1 && len >= 36 + 24)
                {
                    w->loopType  = (int) r.u32 (bodyAt + 36 + 4);
                    w->loopStart = r.u32 (bodyAt + 36 + 8);
                    w->loopEnd   = r.u32 (bodyAt + 36 + 12);
                }
                else w->hasSmpl = w->rootFromFile;   // a smpl with no loop still supplies the root
            }
            // not retained in `chunks`: rewritten from the editor on save
        }
        else if (std::memcmp (id, "bext", 4) == 0)
        {
            if (len >= 256)
            {
                const char* d = (const char*) r.p + bodyAt;
                size_t n = 0; while (n < 256 && d[n] != 0) ++n;
                w->bextDescription.assign (d, n);
            }
        }
        if (std::memcmp (id, "smpl", 4) != 0 && std::memcmp (id, "bext", 4) != 0)
        {
            Chunk c; std::memcpy (c.id, id, 4);
            c.body.assign (r.p + bodyAt, r.p + bodyAt + len);
            w->chunks.push_back (std::move (c));
        }
        at = bodyAt + len + (len & 1);
    }

    if (! haveFmt)  { error = "no fmt chunk"; return nullptr; }
    if (! haveData) { error = "no data chunk"; return nullptr; }
    if (w->channels < 1 || w->channels > 2) { error = "only mono/stereo WAVs are supported (" + std::to_string (w->channels) + " channels)"; return nullptr; }
    const bool pcmOk   = w->formatTag == 1 && (w->bitsPerSample == 8 || w->bitsPerSample == 16 || w->bitsPerSample == 24 || w->bitsPerSample == 32);
    const bool floatOk = w->formatTag == 3 && (w->bitsPerSample == 32 || w->bitsPerSample == 64);
    if (! pcmOk && ! floatOk) { error = "unsupported format (tag " + std::to_string (w->formatTag) + ", " + std::to_string (w->bitsPerSample) + " bit)"; return nullptr; }
    if (w->sampleRate < 1000 || w->sampleRate > 400000) { error = "implausible sample rate"; return nullptr; }
    const int bytesPerSample = w->bitsPerSample / 8;
    if (blockAlign != bytesPerSample * w->channels) blockAlign = bytesPerSample * w->channels;   // tolerate a lying header
    const size_t frames = dataLen / (size_t) blockAlign;
    if (frames < 2) { error = "no audio data"; return nullptr; }
    if (frames > 0x7FFFFFFFu) { error = "file too long"; return nullptr; }

    w->frames = (uint32_t) frames;
    w->left.resize (frames);
    if (w->channels == 2) w->right.resize (frames);
    const uint8_t* s = r.p + dataAt;
    for (size_t f = 0; f < frames; ++f)
    {
        w->left[f] = decodeSample (s, w->bitsPerSample, w->formatTag);
        if (w->channels == 2) w->right[f] = decodeSample (s + bytesPerSample, w->bitsPerSample, w->formatTag);
        s += blockAlign;
    }

    // loop sanity: clamp into the file, degrade a nonsense loop to "none"
    if (w->hasSmpl)
    {
        if (w->loopEnd >= w->frames) w->loopEnd = w->frames - 1;
        if (w->loopStart >= w->loopEnd) { w->loopStart = 0; w->loopEnd = w->frames - 1; }
        if (w->loopType < 0 || w->loopType > 2) w->loopType = 0;
    }
    else
    {
        w->loopStart = 0; w->loopEnd = w->frames - 1;
    }
    if (! w->rootFromFile)
    {
        const int n = rootFromFileName (fileName);
        if (n >= 0) { w->rootKey = n; w->rootFromFile = true; }
        else w->rootKey = 60;
    }
    return w;
}

std::vector<uint8_t> writeWav (const WavSample& w, const WavSaveSpec& spec)
{
    Writer body;   // everything after "WAVE"
    if (spec.export16BitMono)
    {
        // fresh fmt + data: 16-bit PCM mono at the source rate (no resampling)
        Writer fmt;
        fmt.u16 (1); fmt.u16 (1); fmt.u32 (w.sampleRate); fmt.u32 (w.sampleRate * 2); fmt.u16 (2); fmt.u16 (16);
        body.chunk ("fmt ", fmt.b);
        Writer pcm;
        pcm.b.reserve ((size_t) w.frames * 2);
        const float* L = w.L(); const float* R = w.R();
        const float fold = w.isStereo() ? 0.70710678f : 0.5f;   // stereo: -3 dB pan-law fold; mono: L == R so halve
        for (uint32_t i = 0; i < w.frames; ++i)
        {
            const float m = (L[i] + R[i]) * fold;
            const int v = (int) std::lround (std::max (-1.0f, std::min (1.0f, m)) * 32767.0f);
            pcm.u16 ((uint16_t) (int16_t) v);
        }
        // keep any other original chunks (LIST/INFO etc.) -- never fmt/data
        for (const auto& c : w.chunks)
            if (std::memcmp (c.id, "fmt ", 4) != 0 && std::memcmp (c.id, "data", 4) != 0)
                body.chunk (std::string (c.id, 4).c_str(), c.body);
        body.chunk ("data", pcm.b);
    }
    else
    {
        for (const auto& c : w.chunks)
            body.chunk (std::string (c.id, 4).c_str(), c.body);   // byte-identical, original order
    }

    // smpl: one loop, inclusive end, infinite play count
    int root = std::max (0, std::min (127, spec.rootKey));
    double cents = spec.fineCents;
    if (cents < 0.0)  { root = std::max (0, root - 1); cents += 100.0; }
    if (cents >= 100.0) { root = std::min (127, root + 1); cents -= 100.0; }
    const uint32_t lastFrame = w.frames > 0 ? w.frames - 1 : 0;
    const uint32_t ls = std::min (spec.loopStart, lastFrame);
    const uint32_t le = std::max (ls, std::min (spec.loopEnd, lastFrame));
    Writer smpl;
    smpl.u32 (0); smpl.u32 (0);                                             // manufacturer, product
    smpl.u32 (w.sampleRate > 0 ? (uint32_t) (1000000000.0 / w.sampleRate + 0.5) : 0);   // samplePeriod ns
    smpl.u32 ((uint32_t) root);
    smpl.u32 (centsToPitchFraction (cents));
    smpl.u32 (0); smpl.u32 (0);                                             // SMPTE format/offset
    smpl.u32 (1); smpl.u32 (0);                                             // numLoops, samplerData
    smpl.u32 (0);                                                           // cuePointId
    smpl.u32 ((uint32_t) (spec.loopType == 1 ? 1 : 0));                     // type: 0 fwd, 1 alternating
    smpl.u32 (ls); smpl.u32 (le);                                           // start, end (INCLUSIVE)
    smpl.u32 (0); smpl.u32 (0);                                             // fraction, playCount (0 = infinite)
    body.chunk ("smpl", smpl.b);

    // bext (BWF v1): Description, Originator "SF2 Scout", date/time blank-safe, rest zero
    Writer bext;
    bext.fixed (spec.description, 256);
    bext.fixed ("SF2 Scout", 32);
    bext.fixed ("", 32);                      // OriginatorReference
    bext.fixed ("", 10);                      // OriginationDate (filled by the caller through description if wanted)
    bext.fixed ("", 8);                       // OriginationTime
    bext.u32 (0); bext.u32 (0);               // TimeReference
    bext.u16 (1);                             // Version
    for (int i = 0; i < 64; ++i) bext.u8 (0); // UMID
    for (int i = 0; i < 10; ++i) bext.u8 (0); // loudness
    for (int i = 0; i < 180; ++i) bext.u8 (0);// reserved
    body.chunk ("bext", bext.b);

    Writer file;
    file.fcc ("RIFF");
    file.u32 ((uint32_t) (4 + body.b.size()));
    file.fcc ("WAVE");
    file.b.insert (file.b.end(), body.b.begin(), body.b.end());
    return file.b;
}

} // namespace sf2scout
