// ScoutEngine -- the audition voice pool. JUCE-free; real-time safe.
//
// Deliberately NOT a SoundFont synthesizer (docs/DSP.md "What to IGNORE"):
// no envelopes beyond a fixed protective release fade, no LFOs, no filter,
// no modulators, no velocity crossfades. A voice is a read pointer over the
// bank's float pool with linear interpolation and loop logic. Two play modes:
//   AsAuthored : start at the zone's play start, loop per sampleModes
//   LoopOnly   : start AT loopStart and loop [loopStart, loopEnd) from sample 0
//                (no loop defined -> loop the whole sample)
//
// Threading contract:
//   * process()/noteOn()/noteOff()/setBank-consumption run on the AUDIO thread.
//   * setBank() may be called from any thread; the swap happens at the top of
//     the next process() and the previous bank is parked in `retired` for the
//     caller's thread to delete via takeRetiredBank(). No allocation, no locks.
//   * Readout state (last note, playhead, voice count) is published through
//     relaxed atomics for a UI timer to poll.
#pragma once

#include "SoundFontBank.h"
#include <atomic>
#include <array>
#include <cstdint>

namespace sf2scout
{

enum class PlayMode : int { AsAuthored = 0, LoopOnly = 1 };

struct LastNoteInfo
{
    int presetIndex = -1;
    int note        = -1;
    int velocity    = 0;
    int zoneIndex   = -1;        // index into Preset::zones of the zone described
    uint32_t sequence = 0;       // see lastNote() -- advances by 2 per note-on, never odd here
};

class ScoutEngine
{
public:
    static constexpr int kMaxVoices      = 32;
    static constexpr double kReleaseMs   = 80.0;   // protective fade (spec §4)
    static constexpr double kBendRangeSt = 2.0;    // spec §2 (nice-to-have)

    ScoutEngine();
    ~ScoutEngine();

    void prepare (double sampleRate);
    void reset();                                  // all voices off, immediate

    // Hands a bank to the audio thread. Ownership passes to the engine.
    // Any bank the audio thread has finished with is available from
    // takeRetiredBank() -- poll it from the same thread that called setBank().
    void setBank (SoundFontBank* bank);
    SoundFontBank* takeRetiredBank();
    // Message-thread view of the currently *requested* bank (may lag the audio
    // thread by one block). Readout code uses this to resolve zone indices.
    const SoundFontBank* requestedBank() const { return requested_.load (std::memory_order_acquire); }

    // audio-thread API
    void setMode (PlayMode m)     { mode_ = m; }
    void setPreset (int index)    { preset_ = index; }
    void setPitchBend (double semis) { bendSemis_ = semis; }
    void noteOn (int note, int velocity);
    void noteOff (int note);
    void allNotesOff();
    // Renders `numSamples` into left/right (ADDS to them). Applies `gain`.
    void process (float* left, float* right, int numSamples, float gain);

    // polled by the UI
    // F5: lastSeq_ is a classic seqlock, not a plain counter. The writer
    // (noteOn, audio thread) bumps it to an ODD value before touching the
    // fields, writes preset/note/velocity/zone, then bumps it again to the
    // next EVEN value once all four are published. A reader who observes an
    // odd sequence, or a sequence that changed between its first and last
    // load, saw a torn write and must retry -- this is what makes a 4-field
    // read atomic without ever taking a lock on the audio thread. Retries are
    // bounded (kSeqlockRetries) so a UI thread can never spin on this.
    static constexpr int kSeqlockRetries = 8;
    LastNoteInfo lastNote() const;
    int    activeVoiceCount() const { return activeVoices_.load (std::memory_order_relaxed); }
    // playhead of the most recent voice, sample-relative (0..sampleLength), or -1 if it stopped
    double lastPlayhead() const { return lastPlayhead_.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        bool     active   = false;
        bool     releasing = false;
        int      note     = -1;
        uint32_t startOrder = 0;
        const Zone* zone  = nullptr;
        double   pos      = 0.0;      // absolute sample position
        double   step     = 0.0;      // samples advanced per output sample
        double   baseStep = 0.0;      // before pitch bend
        float    gain     = 0.0f;     // velocity gain
        float    panL = 1.0f, panR = 1.0f;
        bool     loop     = false;
        double   loopStart = 0, loopEnd = 0, playEnd = 0;
        float    fade     = 1.0f;     // release multiplier
        float    fadeStep = 0.0f;
        bool     isLatest = false;
    };

    void startVoice (const Zone& z, int note, int velocity);
    void renderVoice (Voice& v, float* left, float* right, int numSamples, float gain);
    Voice* allocateVoice();
    void consumePendingBank();

    std::array<Voice, kMaxVoices> voices_;
    double   sampleRate_ = 44100.0;
    PlayMode mode_       = PlayMode::AsAuthored;
    int      preset_     = 0;
    double   bendSemis_  = 0.0;
    uint32_t orderCounter_ = 0;

    // bank handoff
    SoundFontBank* active_ = nullptr;                       // audio thread only
    std::atomic<SoundFontBank*> pending_  { nullptr };
    std::atomic<SoundFontBank*> retired_  { nullptr };
    std::atomic<const SoundFontBank*> requested_ { nullptr };

    // published state
    std::atomic<int>      activeVoices_ { 0 };
    std::atomic<double>   lastPlayhead_ { -1.0 };
    std::atomic<uint32_t> lastSeq_      { 0 };
    std::atomic<int>      lastPreset_   { -1 };
    std::atomic<int>      lastNoteNum_  { -1 };
    std::atomic<int>      lastVel_      { 0 };
    std::atomic<int>      lastZone_     { -1 };
};

} // namespace sf2scout
