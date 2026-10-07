#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "engine/ScoutEngine.h"
#include "engine/WavSample.h"
#include "engine/ModuleSource.h"
#include "engine/SoundFontBank.h"
#include <map>
#include <memory>
#include <vector>

namespace sf2scout
{

// Parameter IDs are a public API -- append-only, never rename (CLAUDE.md §2).
namespace ParamId
{
    inline constexpr const char* masterGain  = "masterGain";   // float 0..1, default 0.72
    inline constexpr const char* mode        = "mode";         // choice: 0 AS-AUTHORED, 1 LOOP-ONLY
    inline constexpr const char* midiChannel = "midiChannel";  // choice: 0 OMNI, 1..16
}

// v2 model (docs/SCOUT_v2_SPEC.md, handoff v2): several SOURCES are open at
// once; every sample in them is addressed by a SampleKey; the editor edits the
// CURRENT sample; A and B are the two keyboard slots. All of it lives on the
// message thread; the audio thread only sees decoded WavSamples through the
// engine's slot handoff.
struct SampleKey
{
    int src = -1;   // Source::id
    int a = -1;     // SF2: preset index / module: 1-based sample index / WAV: -1
    int b = -1;     // SF2: zone index / else -1
    bool valid() const { return src >= 0; }
    bool operator== (const SampleKey& o) const { return src == o.src && a == o.a && b == o.b; }
    bool operator!= (const SampleKey& o) const { return ! (*this == o); }
    bool operator<  (const SampleKey& o) const { return src != o.src ? src < o.src : (a != o.a ? a < o.a : b < o.b); }
    juce::String toString() const { return juce::String (src) + ":" + juce::String (a) + ":" + juce::String (b); }
    static SampleKey fromString (const juce::String& s);
};

enum class SourceType : int { Sf2 = 0, Module = 1, Wav = 2, Error = 3 };

struct Source
{
    int id = -1;
    SourceType type = SourceType::Error;
    juce::String path, fileName, error;
    std::unique_ptr<SoundFontBank> bank;      // Sf2
    std::unique_ptr<ModuleSource>  module;    // Module
    std::shared_ptr<WavSample>     wav;       // Wav (the file itself)
    int  selPreset = 0;                       // Sf2: the preset the contents list shows
    bool expanded  = true;                    // Sf2: preset rows shown in the source list
    int  sampleCount() const;                 // for the source-list suffix
};

// One row of the CONTENTS list (value snapshot -- never a pointer into a source)
struct ContentsRow
{
    SampleKey key;
    int index = 1;                            // 1-based as shown
    juce::String name;
    int bits = 16, channels = 1;
    uint32_t frames = 0, rate = 44100;
    int loopKind = 0;                         // 0 fwd, 1 ping-pong, 2 off, 3 fwd + sustain loop
    int lo = -1, hi = -1;                     // SF2 zone key range
};

// Per-sample session edits: kept in memory for SF2/module samples, written back
// on SAVE for WAVs (spec "Edits to a source sample's markers live in the session").
struct SampleEdits
{
    uint32_t loopStart = 0, loopEnd = 0;      // inclusive end
    int      loopKind = 0;                    // effective: 0 fwd, 1 pp, 2 off
    bool     loopOverride = false;            // the user changed the kind (F/P/O, LOOP seg)
    int      sourceKind = 0;                  // the source's own kind, 3 = fwd + IT sustain loop
    int      rootKey = 60;
    double   fineCents = 0.0;                 // -99..99
    double   attackMs = 0.0;                  // 0..500
    double   releaseMs = 80.0;                // 10..5000
    juce::String prefix, description;
    bool     dirty = false;                   // unsaved edits (meaningful for WAVs)
};

struct ExportSettings
{
    juce::String folder;                      // current export folder
    int quick = 0;                            // 0 none, 1 reference, 2 rom
    juce::String referenceFolder, romFolder;  // the quick targets' remembered folders
    bool convert16 = false;                   // AUDIO: NATIVE / 16b-44.1 MONO
    int  fold = 0;                            // STEREO: 0 SUM, 1 L ONLY
    uint32_t rangeStart = 0, rangeEnd = 0;    // EXPORT RANGE [start, end)
    int  scope = 0;                           // BATCH: 0 preset|module|file, 1 whole source
    bool skipExisting = true;
};

struct BatchState
{
    bool running = false;
    int done = 0, total = 0, written = 0, skipped = 0;
    juce::String current;                     // "writing 18 Bell Glass"
    juce::StringArray log;                    // newest first, at most 4
    bool lastComplete = false;
};

class ScoutProcessor : public juce::AudioProcessor,
                       private juce::Timer,
                       private juce::AsyncUpdater
{
public:
    ScoutProcessor();
    ~ScoutProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- scout API (message thread unless noted)
    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
    ScoutEngine& engine() { return engine_; }
    const ScoutEngine& engine() const { return engine_; }

    // sources. openSource routes by extension; on failure the previous state is
    // kept AND a red error row is added (README 2.1) -- returns the error text.
    juce::String openSource (const juce::File& file);
    void unloadSource (int id);                                   // also clears A/B/Cur when they point into it
    const std::vector<std::unique_ptr<Source>>& sources() const { return sources_; }
    const Source* source (int id) const;
    Source* source (int id);
    static bool isModuleName (const juce::String& path);
    static bool isSourceName (const juce::String& path);          // sf2 / module / wav
    int sourceGeneration() const { return sourceGeneration_; }    // bumps on every source / selection / slot change

    // selection
    int  selectedSource() const { return selSrc_; }
    void selectSource (int id);                                   // + selects the first sample of it (prototype)
    void selectPreset (int srcId, int preset);
    SampleKey current() const { return cur_; }
    juce::String selectSample (const SampleKey& key);             // decodes into the Cur slot; "" on success
    std::vector<ContentsRow> contents (int srcId) const;          // the CONTENTS list of a source
    juce::String contentsTitle (int srcId, juce::String& kindLabel) const;
    juce::String stepSample (int delta);                          // , . ‹ › in the current contents list

    // A / B slots + keyboard routing
    SampleKey slotKey (int slot) const { return slotKey_[(size_t) slot]; }
    juce::String assignSlot (int slot, const SampleKey& key);     // "" on success
    KbMode kbMode() const { return kbMode_; }
    void setKbMode (KbMode m);
    int  splitNote() const { return splitNote_; }
    void setSplitNote (int n);
    int  toggleOn() const { return toggleOn_; }
    void swapToggle();                                            // TAB: forces TOGGLE and flips
    bool slotLive (int slot) const;                               // the keyboard currently reaches this slot

    // samples
    const WavSample* decoded (const SampleKey& key);              // cached decode (nullptr if it cannot be decoded)
    const SampleEdits* edits (const SampleKey& key) const;        // nullptr until the sample was decoded once
    SampleEdits editsOrDefault (const SampleKey& key);            // decodes on demand
    void setEdits (const SampleKey& key, const SampleEdits& e);   // clamps, pushes to the engine slots holding the key
    juce::String pathOf (const SampleKey& key) const;             // "file ▸ preset ▸ name" / "file ▸ NN name" / "file"
    juce::String nameOf (const SampleKey& key) const;
    int indexOf (const SampleKey& key) const;                     // 1-based
    SourceType typeOf (const SampleKey& key) const;
    juce::String typeBadge (const SampleKey& key) const;          // "SF2" "IT" "XM" "MOD" "S3M" "WAV" ...
    juce::String typeBadge (const Source& s) const;
    bool isWavSource (const SampleKey& key) const;

    // audition (queued to the audio thread, RT-safe)
    void auditionSlot (int slot, int note, bool on, int velocity = 100);
    void auditionKeyboard (int note, bool on, int velocity = 100);   // QWERTY piano: routed like MIDI
    bool latched() const { return latchedNote_ >= 0; }
    void setLatched (bool on);                                    // PLAY / SPACE: the current sample at its root
    int  latchedNote() const { return latchedNote_; }

    // save / export
    juce::String saveWav (const SampleKey& key);                  // WAV sources only: smpl + bext back into the file
    juce::String saveAs (const SampleKey& key, const juce::File& target);
    juce::File   saveAsSuggestion (const SampleKey& key) const;   // <PREFIX>_<NOTE>.wav in the export folder / next to the source
    const ExportSettings& exportSettings() const { return export_; }
    void setExportSettings (const ExportSettings& e);
    juce::String exportFileName (const SampleKey& key) const;     // <SOURCE>_<NN>_<name>_<NOTE>.wav
    juce::String exportFormatNote (const SampleKey& key) const;   // "writes 8287 Hz 8→16-bit mono" / "resample + TPDF dither → ..."
    juce::String exportSample (const SampleKey& key, juce::String& writtenPath);
    juce::String exportRange (const SampleKey& key, juce::String& writtenPath, bool& markersDropped);
    const BatchState& batch() const { return batch_; }
    juce::String startBatch();                                    // "" on success; runs on the timer, one file per tick
    void cancelBatch();
    juce::String batchScopeLabel() const;                         // "PRESET" / "MODULE" / "FILE"

    // metadata box
    struct MetaRow { juce::String k, v; };
    std::vector<MetaRow> metadata (int srcId) const;

    // standalone MIDI input device (identifier; "" = all devices)
    juce::String midiInputDevice() const { return midiInputDevice_; }
    void setMidiInputDevice (const juce::String& id) { midiInputDevice_ = id; }
    // UI scale factor (non-param tree state)
    double uiScale() const { return uiScale_; }
    void setUiScale (double k) { uiScale_ = juce::jlimit (0.75, 1.5, k); }

    static constexpr const char* kBuildStamp = SF2SCOUT_VERSION_STRING " " __DATE__ " " __TIME__;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void performPendingRestore();

    // sources
    Source* addSource (SourceType type, const juce::File& file);
    void addErrorRow (const juce::File& file, const juce::String& error);
    juce::String loadFileBytes (const juce::File& file, juce::MemoryBlock& out) const;
    std::vector<SampleKey> keysOf (const Source& s, int preset, bool wholeSource) const;
    std::unique_ptr<WavSample> decodeFresh (const SampleKey& key, std::string& error) const;
    SampleEdits defaultEdits (const SampleKey& key, const WavSample& w) const;
    juce::String provenance (const SampleKey& key, const WavSample& w, const SampleEdits& e) const;
    void bump() { ++sourceGeneration_; }

    // engine slots
    void installSlot (int slot, const SampleKey& key);            // decodes (cached), installs, pushes the edits
    void clearSlot (int slot);
    void pushEdits (int slot, const SampleEdits& e);
    void pushKeyboard();
    void collectRetired();
    void releaseHeld (WavSample* p);                              // one engine reference less on a shared sample
    void unlatch();

    // export helpers
    ExportOptions exportOptionsFor (const SampleKey& key, bool range) const;
    WavSaveSpec  saveSpecFor (const SampleKey& key, const WavSample& w, const SampleEdits& e) const;
    juce::String writeBytes (const juce::File& target, const std::vector<uint8_t>& bytes) const;
    juce::File   exportFolderOrDefault (const SampleKey& key) const;
    void batchTick();

    juce::AudioProcessorValueTreeState apvts_;
    ScoutEngine engine_;
    std::atomic<float>* gainParam_    = nullptr;
    std::atomic<float>* modeParam_    = nullptr;
    std::atomic<float>* channelParam_ = nullptr;
    juce::SmoothedValue<float> gainSmoother_;
    int lastChannelFilter_ = -1;

    std::vector<std::unique_ptr<Source>> sources_;
    int nextSourceId_ = 1;
    int selSrc_ = -1;
    SampleKey cur_;
    SampleKey slotKey_[kSlots];
    KbMode kbMode_ = KbMode::A;
    int splitNote_ = 60;
    int toggleOn_ = 0;
    int latchedNote_ = -1;
    int sourceGeneration_ = 0;
    std::map<SampleKey, std::shared_ptr<WavSample>> decoded_;
    std::map<SampleKey, SampleEdits> edits_;
    // samples the engine may still reference (kept alive until every slot retired them)
    std::map<const WavSample*, std::pair<std::shared_ptr<WavSample>, int>> engineHeld_;
    ExportSettings export_;
    BatchState batch_;
    std::vector<SampleKey> batchQueue_;
    juce::String midiInputDevice_;
    double uiScale_ = 1.0;
    std::unique_ptr<juce::PropertiesFile> settings_;              // export folders survive across instances

    // restore (message thread)
    juce::ValueTree pendingRestoreTree_;

    // UI -> audio thread note queue (lock-free, single producer / single consumer)
    struct UiNote { int slot; int note; int velocity; bool on; };  // slot -1 = keyboard routing
    juce::AbstractFifo uiNoteFifo_ { 64 };
    std::array<UiNote, 64> uiNotes_ {};
    std::atomic<bool> uiPanic_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoutProcessor)
};

} // namespace sf2scout
