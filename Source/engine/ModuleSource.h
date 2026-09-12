// ModuleSource -- a tracker module (.mod/.xm/.it/.s3m + every libopenmpt format)
// opened as a read-only CONTAINER of samples (docs/SCOUT_v2_SPEC.md "Sources").
//
// JUCE-free on purpose (the cl.exe/CMake harness compiles this without JUCE).
// Everything runs on the MESSAGE thread: a module is parsed once by the vendored
// OpenMPT soundlib (third_party/libopenmpt, BSD-3) and each sample can then be
// DECODED into a WavSample -- the one shape the engine, the loop editor and the
// exporter already understand. The module file itself is never written.
//
// Tuning contract: a tracker sample has no root key; it has a C-5 frequency
// (IT/S3M nC5Speed, or MOD/XM finetune+relative-note folded to Hz by the
// soundlib). We keep that frequency as the WAV's sample rate and declare the
// root as MIDI 60 with 0 cents, so the WAV plays at native pitch on middle C
// exactly like the tracker would -- and the exported file carries the tuning
// in its fmt chunk rather than in a lossy root/cents pair.
//
// Loop contract: libopenmpt stores loop ends EXCLUSIVE; WavSample wants the
// INCLUSIVE last sample (RIFF/Cubase reading). An IT sustain loop is what sounds
// while a key is held, so when both loops exist the sustain loop is the one the
// decoded sample carries; both are reported in ModuleSampleInfo.
#pragma once

#include "WavSample.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf2scout
{

struct ModuleSampleInfo
{
    int         index = 0;               // 1-based, as trackers number them
    std::string name;                    // sample name (may be empty)
    std::string fileName;                // sample's own DOS filename field (IT/S3M), may be empty
    uint32_t    frames     = 0;          // 0 = slot is empty (no data)
    int         bits       = 8;          // 8 or 16 (data as stored in the module)
    int         channels   = 1;
    uint32_t    sampleRate = 8363;       // C-5 frequency in Hz (see header)
    bool        hasLoop    = false;
    bool        pingPong   = false;
    uint32_t    loopStart  = 0;          // frames
    uint32_t    loopEnd    = 0;          // frames, EXCLUSIVE (as stored)
    bool        hasSustain = false;      // IT sustain loop
    bool        sustainPingPong = false;
    uint32_t    sustainStart = 0;
    uint32_t    sustainEnd   = 0;        // exclusive
    int         defaultVolume = 64;      // 0..64 as trackers show it
    bool isEmpty() const { return frames == 0; }
};

class ModuleSource
{
public:
    ~ModuleSource();

    // Parses an in-memory module image. Returns nullptr and fills `error` on
    // any failure -- malformed input never throws out and never crashes.
    static std::unique_ptr<ModuleSource> load (const void* data, size_t size, const std::string& fileName, std::string& error);

    // True when the extension (with or without the dot, any case) is a format
    // the soundlib can open. Drives drag-and-drop routing and the file filter.
    static bool isSupportedExtension (const std::string& ext);
    // "*.mod;*.xm;..." style list for a file chooser (no leading dots).
    static std::vector<std::string> supportedExtensions();

    const std::string& fileName()   const { return fileName_; }
    const std::string& title()      const { return title_; }
    const std::string& formatName() const { return formatName_; }     // "FastTracker 2"
    const std::string& formatType() const { return formatType_; }     // "xm"
    const std::string& madeWith()   const { return madeWith_; }
    const std::string& message()    const { return message_; }        // song message, LF line endings
    const std::vector<std::string>& instrumentNames() const { return instrumentNames_; }

    const std::vector<ModuleSampleInfo>& samples() const { return samples_; }
    int sampleCount() const { return (int) samples_.size(); }
    int nonEmptySampleCount() const;
    // index of the first non-empty sample, or -1
    int firstNonEmpty() const;

    // Decodes sample `index` (1-based) to 16-bit PCM in a WavSample with the
    // module's loop points, tuning and provenance. nullptr + `error` if the
    // slot is empty or out of range.
    std::unique_ptr<WavSample> decode (int index, std::string& error) const;

private:
    ModuleSource() = default;
    struct Impl;
    Impl* impl_ = nullptr;
    std::string fileName_, title_, formatName_, formatType_, madeWith_, message_;
    std::vector<std::string> instrumentNames_;
    std::vector<ModuleSampleInfo> samples_;
};

} // namespace sf2scout
