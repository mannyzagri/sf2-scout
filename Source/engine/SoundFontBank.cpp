#include "SoundFontBank.h"

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#include "../../third_party/tsf/tsf.h"

#include <cstring>
#include <algorithm>

namespace sf2scout
{

namespace
{
    struct ShdrRec
    {
        char     name[21];
        uint32_t start, end, startLoop, endLoop, sampleRate;
        uint8_t  originalPitch;
        int8_t   pitchCorrection;
        uint16_t sampleLink, sampleType;
    };

    inline uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    inline bool     fourcc (const uint8_t* p, const char* id) { return std::memcmp (p, id, 4) == 0; }

    // Minimal RIFF walk: RIFF/sfbk -> LIST/pdta -> shdr. Returns false if the
    // structure is not there; bounds-checked at every step.
    bool readShdr (const uint8_t* data, size_t size, std::vector<ShdrRec>& out)
    {
        if (size < 12 || ! fourcc (data, "RIFF") || ! fourcc (data + 8, "sfbk")) return false;
        size_t riffEnd = std::min (size, (size_t) 8 + rd32 (data + 4));
        size_t pos = 12;
        while (pos + 8 <= riffEnd)
        {
            uint32_t csize = rd32 (data + pos + 4);
            size_t   cbody = pos + 8;
            size_t   cend  = cbody + csize;
            if (cend > riffEnd) return false;
            if (fourcc (data + pos, "LIST") && csize >= 4 && fourcc (data + cbody, "pdta"))
            {
                size_t p = cbody + 4;
                while (p + 8 <= cend)
                {
                    uint32_t s = rd32 (data + p + 4);
                    size_t   b = p + 8;
                    if (b + s > cend) return false;
                    if (fourcc (data + p, "shdr"))
                    {
                        const uint32_t n = s / 46;
                        out.reserve (n);
                        for (uint32_t i = 0; i < n; ++i)
                        {
                            const uint8_t* r = data + b + (size_t) i * 46;
                            ShdrRec h {};
                            std::memcpy (h.name, r, 20); h.name[20] = 0;
                            h.start = rd32 (r + 20); h.end = rd32 (r + 24);
                            h.startLoop = rd32 (r + 28); h.endLoop = rd32 (r + 32);
                            h.sampleRate = rd32 (r + 36);
                            h.originalPitch = r[40]; h.pitchCorrection = (int8_t) r[41];
                            h.sampleLink = rd16 (r + 42); h.sampleType = rd16 (r + 44);
                            out.push_back (h);
                        }
                        return true;
                    }
                    p = b + s + (s & 1);
                }
            }
            pos = cend + (csize & 1);
        }
        return false;
    }

    // Join a region back to its sample header: the shdr whose [start,end]
    // contains the region's play offset (TSF adds shdr.start to it).
    int shdrForRegion (const std::vector<ShdrRec>& shdrs, const tsf_region& r)
    {
        int best = -1;
        for (size_t i = 0; i < shdrs.size(); ++i)
        {
            const auto& h = shdrs[i];
            if (h.sampleType & 0x8000) continue;                // ROM sample -- ignored (spec)
            if (r.offset >= h.start && r.offset < h.end)
            {
                // prefer the tightest match (samples can nest in broken files)
                if (best < 0 || (h.end - h.start) < (shdrs[(size_t) best].end - shdrs[(size_t) best].start))
                    best = (int) i;
            }
        }
        return best;
    }
}

SoundFontBank::~SoundFontBank()
{
    if (font_ != nullptr)
        tsf_close (font_);
}

std::unique_ptr<SoundFontBank> SoundFontBank::load (const void* data, size_t size, std::string& error)
{
    error.clear();
    if (data == nullptr || size < 12)
    {
        error = "file is empty or too small to be a SoundFont";
        return nullptr;
    }
    if (size > 0x7fffffffu)
    {
        error = "file is larger than 2 GB";
        return nullptr;
    }

    std::vector<ShdrRec> shdrs;
    if (! readShdr ((const uint8_t*) data, size, shdrs))
    {
        error = "not a SoundFont 2 file (no RIFF/sfbk/pdta/shdr structure)";
        return nullptr;
    }

    tsf* f = tsf_load_memory (data, (int) size);
    if (f == nullptr)
    {
        error = "SoundFont is incomplete or truncated (TinySoundFont refused it)";
        return nullptr;
    }

    std::unique_ptr<SoundFontBank> bank (new SoundFontBank());
    bank->font_ = f;
    bank->samples_ = f->fontSamples;

    // TSF does not keep the sample count; recover it from the headers
    // (every region end has already been clamped to it by TSF).
    uint32_t maxEnd = 0;
    for (const auto& h : shdrs) if (! (h.sampleType & 0x8000)) maxEnd = std::max (maxEnd, h.end);
    for (int p = 0; p < f->presetNum; ++p)
        for (int r = 0; r < f->presets[p].regionNum; ++r)
            maxEnd = std::max (maxEnd, f->presets[p].regions[r].end);
    bank->sampleCount_ = maxEnd;

    bank->presets_.reserve ((size_t) f->presetNum);
    for (int p = 0; p < f->presetNum; ++p)
    {
        const tsf_preset& tp = f->presets[p];
        Preset preset;
        preset.bank    = tp.bank;
        preset.program = tp.preset;
        preset.name.assign (tp.presetName, strnlen (tp.presetName, 20));
        preset.zones.reserve ((size_t) tp.regionNum);

        for (int r = 0; r < tp.regionNum; ++r)
        {
            const tsf_region& tr = tp.regions[r];
            Zone z;
            z.lokey = tr.lokey; z.hikey = tr.hikey; z.lovel = tr.lovel; z.hivel = tr.hivel;
            z.rootKey   = tr.pitch_keycenter;
            z.transpose = tr.transpose;
            z.tuneCents = tr.tune;                     // already includes shdr.pitchCorrection
            z.keytrack  = tr.pitch_keytrack;
            z.sampleRate = tr.sample_rate;
            z.playStart = tr.offset;
            z.playEnd   = tr.end;
            z.loopStart = tr.loop_start;
            z.loopEnd   = tr.loop_end + 1;            // TSF stores inclusive last index
            z.loopMode  = (LoopMode) tr.loop_mode;
            z.pan       = tr.pan;

            const int si = shdrForRegion (shdrs, tr);
            z.sampleIndex = si;
            if (si >= 0)
            {
                const auto& h = shdrs[(size_t) si];
                z.sampleName  = h.name;
                z.sampleStart = h.start;
                z.sampleEnd   = std::min (h.end, maxEnd);
                z.sampleType  = h.sampleType;
                if (z.sampleRate == 0) z.sampleRate = h.sampleRate;
            }
            else
            {
                z.sampleName  = "(sample " + std::to_string (r) + ")";
                z.sampleStart = tr.offset;
                z.sampleEnd   = tr.end;
            }
            // sanitise geometry so the player can trust it blindly
            z.playEnd   = std::min (z.playEnd, maxEnd);
            z.playStart = std::min (z.playStart, z.playEnd);
            z.loopEnd   = std::min (z.loopEnd, z.playEnd);
            if (z.loopStart >= z.loopEnd) z.loopMode = LoopMode::None;
            if (z.sampleRate == 0) z.sampleRate = 44100;
            preset.zones.push_back (std::move (z));
        }
        bank->presets_.push_back (std::move (preset));
    }

    // Stable, readable order: bank then program (TSF keeps file order).
    std::stable_sort (bank->presets_.begin(), bank->presets_.end(),
                      [] (const Preset& a, const Preset& b)
                      { return a.bank != b.bank ? a.bank < b.bank : a.program < b.program; });

    if (bank->presets_.empty())
    {
        error = "SoundFont contains no presets";
        return nullptr;
    }
    return bank;
}

int SoundFontBank::findZones (int presetIndex, int key, int vel, const Zone** out, int maxOut) const
{
    if (presetIndex < 0 || presetIndex >= (int) presets_.size() || maxOut <= 0) return 0;
    int n = 0;
    for (const auto& z : presets_[(size_t) presetIndex].zones)
    {
        if (z.coversKey (key) && z.coversVel (vel) && z.playEnd > z.playStart)
        {
            out[n++] = &z;
            if (n == maxOut) break;
        }
    }
    return n;
}

const Zone* SoundFontBank::zoneForKey (int presetIndex, int key, int vel) const
{
    if (presetIndex < 0 || presetIndex >= (int) presets_.size()) return nullptr;
    for (const auto& z : presets_[(size_t) presetIndex].zones)
        if (z.coversKey (key) && (vel < 0 || z.coversVel (vel)))
            return &z;
    return nullptr;
}

} // namespace sf2scout
