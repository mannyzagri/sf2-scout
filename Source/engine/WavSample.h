// WavSample -- Slot W's source: one WAV file fully decoded to float, plus the
// `smpl` / `bext` metadata the loop editor reads and rewrites.
//
// JUCE-free on purpose (tests/test_engine.cpp compiles this with cl.exe alone).
// Everything here runs on the MESSAGE thread; the audio thread only ever reads
// a finished WavSample through ScoutEngine's pointer handoff (ScoutEngine::setWav).
//
// Save contract (docs/SF2SCOUT_WAV_EXTENSION.md §1): the rewrite keeps every
// original chunk byte-identical (fmt, data, anything else) EXCEPT `smpl` and
// `bext`, which are dropped and re-emitted from the editor's values. loopEnd is
// stored INCLUSIVE (the last sample index that plays) -- the RIFF spec's and
// Cubase's reading. This is the ONLY writer in the project; it can only ever be
// fed a WavSample, never SF2 pool data (no-SF2-export rule, CLAUDE.md §2.1).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf2scout
{

struct WavSample
{
    // decoded audio -- `left` always filled; `right` empty for mono
    std::vector<float> left, right;
    uint32_t frames     = 0;
    uint32_t sampleRate = 44100;
    int      channels   = 1;
    int      bitsPerSample = 16;
    int      formatTag  = 1;         // 1 PCM, 3 IEEE float (extensible resolved to its sub-format)
    std::string fileName;

    // metadata as read from the file (defaults if absent)
    bool     hasSmpl   = false;
    uint32_t loopStart = 0;          // frames, inclusive
    uint32_t loopEnd   = 0;          // frames, INCLUSIVE last sample of the loop
    int      loopType  = 0;          // 0 forward, 1 ping-pong (alternating), 2 backward
    int      rootKey   = 60;         // dwMIDIUnityNote (or the filename / 60 fallback)
    double   fineCents = 0.0;        // dwMIDIPitchFraction, normalised to [-50, 50)
    bool     rootFromFile = false;   // true when rootKey came from smpl or the file name
    std::string bextDescription;     // existing bext Description (may be empty)

    // every original chunk except smpl/bext, in file order, for byte-identical rewrite
    struct Chunk { char id[4]; std::vector<uint8_t> body; };
    std::vector<Chunk> chunks;

    const float* L() const { return left.data(); }
    const float* R() const { return right.empty() ? left.data() : right.data(); }
    bool isStereo() const  { return ! right.empty(); }

    // Parses a whole .wav image. Returns nullptr and fills `error` on failure.
    // Malformed input never throws or crashes. `fileName` (basename) feeds the
    // <PREFIX>_<NOTE>.wav root fallback when no smpl chunk carries a root.
    static std::unique_ptr<WavSample> load (const void* data, size_t size, const std::string& fileName, std::string& error);

    // Builds a WavSample from decoded 16-bit PCM (interleaved when stereo) --
    // the bridge every non-WAV source (SF2 zone, module sample) uses to enter
    // the editor and the exporter. Synthesises fmt + data chunks so writeWav
    // can serialise it; loop/root fields are left at their defaults (whole
    // file, root 60) for the caller to fill.
    static std::unique_ptr<WavSample> fromPcm16 (std::vector<int16_t> interleaved, int channels, uint32_t sampleRate, const std::string& fileName);
};

// What the editor wants written back.
struct WavSaveSpec
{
    uint32_t loopStart = 0;
    uint32_t loopEnd   = 0;          // inclusive
    int      loopType  = 0;          // 0 forward, 1 ping-pong
    int      rootKey   = 60;
    double   fineCents = 0.0;        // [-50, 50)
    std::string description;         // bext Description (<= 256 bytes, truncated)
    bool     export16BitMono = false; // re-encode audio as 16-bit PCM mono (-3 dB fold); else byte-identical
};

// Serialises `w` + `spec` as a complete RIFF/WAVE image. Never fails on valid input.
// This is the SAVE path: audio bytes stay identical (unless export16BitMono).
std::vector<uint8_t> writeWav (const WavSample& w, const WavSaveSpec& spec);

// EXPORT path (docs/SCOUT_v2_SPEC.md "Export / convert"): re-encodes the audio
// from the decoded floats, so it can cut a RANGE, convert to 16-bit / 44.1 kHz
// mono (windowed-sinc resampling + TPDF dither) and fold stereo (sum at -3 dB,
// or the left channel only). NATIVE keeps the source rate and bit depth (8-bit
// sources come out 16-bit, floats stay float). Loop markers travel with the
// audio: remapped into the range, scaled by the resampling ratio; a loop that
// falls outside the range is dropped (smpl keeps the root, no loop).
struct ExportOptions
{
    bool     convert16Bit441Mono = false;
    int      stereoFold = 0;                 // 0 sum (-3 dB), 1 left only -- used whenever the output is mono
    bool     hasRange = false;
    uint32_t rangeStart = 0, rangeEnd = 0;   // [start, end) frames of the source
    std::string originator = "Scout v2";     // bext Originator (<= 32)
    std::string originationDate;             // "yyyy-mm-dd" (bext, may be empty)
    std::string originationTime;             // "hh:mm:ss"
};

struct ExportResult
{
    std::vector<uint8_t> bytes;
    uint32_t frames = 0;
    uint32_t sampleRate = 0;
    int      channels = 1;
    int      bits = 16;
    bool     loopKept = false;               // false = markers fell outside the range (smpl written without a loop)
    uint32_t loopStart = 0, loopEnd = 0;     // as written (inclusive end)
    std::string error;                       // non-empty = nothing written (empty range, no audio)
};

ExportResult exportWav (const WavSample& w, const WavSaveSpec& spec, const ExportOptions& opt);

// "<PREFIX>_<NOTE>.wav" -> MIDI note (C4 = 60, sharps '#', flats 'b', negative
// octaves "C-1"), or -1 if the name carries no note suffix.
int rootFromFileName (const std::string& fileName);

// dwMIDIPitchFraction <-> cents. The RIFF field is an unsigned fraction of a
// semitone (0x80000000 = 50 cents), always upward; a negative fine tune is
// stored as (root - 1, cents + 100) and normalised back on read.
uint32_t centsToPitchFraction (double cents);      // expects 0 <= cents < 100
double   pitchFractionToCents (uint32_t fraction);

} // namespace sf2scout
