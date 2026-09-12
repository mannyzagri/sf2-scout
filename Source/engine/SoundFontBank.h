// SoundFontBank -- an immutable, fully-parsed SF2 held in memory.
//
// JUCE-free on purpose: tests/test_engine.cpp compiles this with cl.exe alone.
// TinySoundFont (third_party/tsf/tsf.h) does the RIFF/hydra walk and the
// generator merging and hands us flat tsf_region records + a float sample pool.
// TSF frees the hydra after loading, and tsf_region carries no sample NAME and
// no stereo flag -- so this class walks the `shdr` chunk itself (it is a flat
// 46-byte record array) and joins each region back to its sample header by
// offset. Everything the readout shows comes from this join.
//
// Ownership: one bank per loaded file. The audio thread only ever READS a bank;
// swapping banks is the engine's job (ScoutEngine::setBank).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "WavSample.h"

struct tsf;

namespace sf2scout
{

enum class LoopMode : int { None = 0, Continuous = 1, Sustain = 2 };

// One zone = one tsf_region joined to its sample header.
// All sample positions are ABSOLUTE indices into SoundFontBank::samples().
struct Zone
{
    // identity
    int         sampleIndex  = -1;      // shdr index
    std::string sampleName;             // shdr.sampleName (<=20 chars)
    // key/velocity coverage
    int lokey = 0, hikey = 127, lovel = 0, hivel = 127;
    // pitch
    int    rootKey   = 60;              // overridingRootKey else shdr.originalPitch
    int    transpose = 0;               // coarseTune (semitones)
    double tuneCents = 0.0;             // fineTune + shdr.pitchCorrection (cents)
    int    keytrack  = 100;             // scaleTuning (percent)
    // sample geometry (absolute, in the shared float pool)
    uint32_t sampleStart = 0;           // shdr.start
    uint32_t sampleEnd   = 0;           // shdr.end (exclusive, one past last)
    uint32_t playStart   = 0;           // region offset (start + startAddrsOffset gens)
    uint32_t playEnd     = 0;           // exclusive
    uint32_t loopStart   = 0;           // absolute, inclusive
    uint32_t loopEnd     = 0;           // absolute, EXCLUSIVE (SF2 endLoop semantics)
    LoopMode loopMode    = LoopMode::None;
    uint32_t sampleRate  = 44100;
    // stereo pair info (shdr.sampleType: 1 mono, 2 right, 4 left, 8 linked)
    int  sampleType = 1;
    bool isStereoHalf() const { return sampleType == 2 || sampleType == 4; }
    float pan = 0.0f;                   // -1 .. +1 from the region's pan generator

    // derived helpers
    uint32_t sampleLength() const { return sampleEnd > sampleStart ? sampleEnd - sampleStart : 0; }
    bool     hasLoop()      const { return loopMode != LoopMode::None && loopEnd > loopStart + 1; }
    uint32_t loopLength()   const { return hasLoop() ? loopEnd - loopStart : 0; }
    // sample-relative positions for display
    uint32_t loopStartRel() const { return loopStart - sampleStart; }
    uint32_t loopEndRel()   const { return loopEnd - sampleStart; }
    bool coversKey(int key) const { return key >= lokey && key <= hikey; }
    bool coversVel(int vel) const { return vel >= lovel && vel <= hivel; }
};

struct Preset
{
    int bank = 0, program = 0;
    std::string name;
    std::vector<Zone> zones;           // in file order
};

class SoundFontBank
{
public:
    ~SoundFontBank();

    // Parses an in-memory SF2. Returns nullptr and fills `error` on any
    // failure -- malformed input never throws and never crashes (spec §1).
    static std::unique_ptr<SoundFontBank> load (const void* data, size_t size, std::string& error);

    const std::string& fileName() const { return fileName_; }
    void setFileName (std::string n) { fileName_ = std::move (n); }

    const std::vector<Preset>& presets() const { return presets_; }
    int presetCount() const { return (int) presets_.size(); }

    const float* samples()     const { return samples_; }
    uint32_t     sampleCount() const { return sampleCount_; }

    // Zones of `preset` covering key+vel, in file order (a stereo sample gives 2).
    // Returns the number written; never more than `maxOut`.
    int findZones (int presetIndex, int key, int vel, const Zone** out, int maxOut) const;

    // The zone the READOUT should describe for a key: first zone of the preset
    // covering key (velocity-agnostic when vel < 0). nullptr if none.
    const Zone* zoneForKey (int presetIndex, int key, int vel = -1) const;

    // v2 export path (docs/SCOUT_v2_SPEC.md): copies one zone's sample out of
    // the float pool as a 16-bit WavSample carrying the zone's loop (inclusive
    // end), root/tune and provenance, so it can enter Slot W and the exporter.
    // The bank itself is never written.
    std::unique_ptr<WavSample> decodeZone (const Zone& z) const;

private:
    SoundFontBank() = default;
    tsf*         font_        = nullptr;
    const float* samples_     = nullptr;
    uint32_t     sampleCount_ = 0;
    std::vector<Preset> presets_;
    std::string  fileName_;
};

} // namespace sf2scout
