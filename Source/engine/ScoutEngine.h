// ScoutEngine -- the audition voice pool. JUCE-free; real-time safe.
//
// v2 model (docs/SCOUT_v2_SPEC.md "Playback / audition", handoff v2): every
// source sample -- a WAV, a decoded SF2 zone, a decoded tracker sample -- enters
// the engine as a WavSample in one of three SLOTS:
//   A, B : the two selection slots the MIDI keyboard plays (KbMode A / B /
//          SPLIT below-above a split note / TOGGLE, TAB swaps)
//   Cur  : the editor's current sample -- PLAY / SPACE latch, double-click,
//          zone-strip click audition it regardless of the keyboard routing
// A voice is a read pointer over its slot's float data with linear
// interpolation, forward / ping-pong / off looping, ATTACK fade-in, RELEASE fade
// (the loop keeps cycling under it), and two audition variants:
//   AsAuthored : start at frame 0, loop per the slot's loop kind
//   LoopOnly   : start AT loopStart and loop [loopStart, loopEnd]
//
// Threading contract:
//   * process()/noteOn*()/noteOff*() run on the AUDIO thread.
//   * setSlot() may be called from any thread; the swap happens at the top of
//     the next process() and the previous sample is parked in that slot's
//     retiree for the caller's thread to collect via takeRetired(). No
//     allocation, no locks. Live edit state travels through atomics.
//   * Readout state (last note, playhead, voice count) is published through
//     relaxed atomics for a UI timer to poll.
#pragma once

#include "WavSample.h"
#include <atomic>
#include <array>
#include <cstdint>

namespace sf2scout
{

enum class PlayMode : int { AsAuthored = 0, LoopOnly = 1 };
enum class LoopKind : int { Forward = 0, PingPong = 1, Off = 2 };
enum class KbMode   : int { A = 0, B = 1, Split = 2, Toggle = 3 };

// slot indices (also the order the UI shows them)
constexpr int kSlotA = 0, kSlotB = 1, kSlotCur = 2, kSlots = 3;

class ScoutEngine
{
public:
    static constexpr int kMaxVoices      = 16;      // spec: "16 voices; oldest-steal"
    static constexpr double kBendRangeSt = 2.0;

    ScoutEngine();
    ~ScoutEngine();

    void prepare (double sampleRate);
    void reset();                                  // all voices off, immediate

    // ---- slot handoff. Ownership passes in; any sample the audio thread has
    // finished with comes back through takeRetired(slot) -- poll it from the
    // thread that called setSlot(). One retiree per slot: a second swap waits
    // (keeps playing the current sample) until the collector caught up.
    // A sample that was still PENDING (never reached the audio thread) when it
    // is superseded or cleared is handed straight back to the caller as the
    // return value -- the engine never deletes what it never played.
    WavSample* setSlot (int slot, WavSample* sample);
    WavSample* takeRetired (int slot);
    WavSample* clearSlot (int slot);               // UNLOAD: voices die, sample retired; returns a dropped pending one
    // Owner teardown only (audio thread stopped): forget every pointer without
    // deleting -- for an owner that shares its samples elsewhere.
    void forgetAll();
    // Message-thread view of the *requested* sample (may lag the audio thread by one block).
    const WavSample* requested (int slot) const { return slots_[(size_t) slot].requested.load (std::memory_order_acquire); }
    bool hasSlot (int slot) const { return requested (slot) != nullptr; }

    // ---- live edit state, any thread -> read by the audio thread at the top of
    // each process() (loop points packed into ONE atomic so they never tear).
    void setSlotLoop (int slot, uint32_t startFrame, uint32_t endFrameInclusive, LoopKind kind);
    void setSlotTuning (int slot, int rootKey, double fineCents);
    void setSlotFades (int slot, double attackMs, double releaseMs);

    // ---- keyboard routing (any thread). toggleOn: 0 = A sounds, 1 = B sounds.
    void setKeyboard (KbMode mode, int splitNote, int toggleOn);
    // MIDI note -> slot index (kSlotA / kSlotB) or -1 (nothing to play). Pure;
    // the UI uses it too. An EMPTY target falls back to the other loaded slot so
    // a routing left on an unloaded slot can never mute the keyboard.
    static int routeSlot (KbMode mode, int splitNote, int toggleOn, int note, bool haveA, bool haveB);

    // ---- audio-thread API
    void setMode (PlayMode m)        { mode_ = m; }
    PlayMode mode() const            { return mode_; }
    void setPitchBend (double semis) { bendSemis_ = semis; }
    void noteOn (int note, int velocity);                   // keyboard: routed to A / B
    void noteOnSlot (int slot, int note, int velocity);     // audition a slot directly (PLAY, zone click)
    void noteOff (int note);                                // every voice on that note, any slot
    void noteOffSlot (int slot, int note);
    void allNotesOff();
    // Renders `numSamples` into left/right (ADDS to them). Applies `gain`.
    void process (float* left, float* right, int numSamples, float gain);

    // ---- polled by the UI
    int    activeVoiceCount() const        { return activeVoices_.load (std::memory_order_relaxed); }
    // playhead of the slot's most recent voice in frames, or -1 if none is sounding
    double lastPlayhead (int slot) const   { return slots_[(size_t) slot].lastPlayhead.load (std::memory_order_relaxed); }
    int    lastNote (int slot) const       { return slots_[(size_t) slot].lastNote.load (std::memory_order_relaxed); }
    int    lastVelocity (int slot) const   { return slots_[(size_t) slot].lastVel.load (std::memory_order_relaxed); }
    // advances by one per note-on on that slot -- the UI diffs it to notice new notes
    uint32_t noteCounter (int slot) const  { return slots_[(size_t) slot].noteSeq.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        bool     active    = false;
        bool     releasing = false;
        int      slot      = -1;
        int      note      = -1;
        uint32_t startOrder = 0;
        double   pos       = 0.0;
        double   step      = 0.0;
        double   baseStep  = 0.0;
        int      dir       = 1;
        float    gain      = 0.0f;
        float    fade      = 1.0f;
        float    fadeStep  = 0.0f;
        float    attack    = 1.0f;
        float    attackStep = 0.0f;
        bool     isLatest  = false;
    };

    struct Slot
    {
        WavSample* active = nullptr;                          // audio thread only
        std::atomic<WavSample*> pending  { nullptr };
        std::atomic<WavSample*> retired  { nullptr };
        std::atomic<const WavSample*> requested { nullptr };
        std::atomic<bool> clear { false };
        std::atomic<uint64_t> loop { 0 };                     // (start << 32) | endInclusive
        std::atomic<int>      kind { (int) LoopKind::Forward };
        std::atomic<int>      root { 60 };
        std::atomic<double>   cents { 0.0 };
        std::atomic<double>   attackMs { 0.0 };
        std::atomic<double>   releaseMs { 80.0 };
        // per-block snapshot (audio thread only)
        double curStart = 0.0, curEnd = 0.0;
        LoopKind curKind = LoopKind::Forward;
        // published
        std::atomic<double>   lastPlayhead { -1.0 };
        std::atomic<int>      lastNote { -1 };
        std::atomic<int>      lastVel { 0 };
        std::atomic<uint32_t> noteSeq { 0 };
    };

    void consumePending (Slot& s);
    void startVoice (int slot, int note, int velocity);
    void renderVoice (Voice& v, float* left, float* right, int numSamples, float gain);
    Voice* allocateVoice();
    void beginRelease (Voice& v);

    std::array<Voice, kMaxVoices> voices_;
    std::array<Slot, kSlots> slots_;
    double   sampleRate_ = 44100.0;
    PlayMode mode_       = PlayMode::AsAuthored;
    double   bendSemis_  = 0.0;
    uint32_t orderCounter_ = 0;
    std::atomic<int> kbMode_   { (int) KbMode::A };
    std::atomic<int> split_    { 60 };
    std::atomic<int> toggleOn_ { 0 };
    std::atomic<int> activeVoices_ { 0 };
};

} // namespace sf2scout
