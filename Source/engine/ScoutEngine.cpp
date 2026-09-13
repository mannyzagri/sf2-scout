#include "ScoutEngine.h"
#include <cmath>
#include <algorithm>

namespace sf2scout
{

ScoutEngine::ScoutEngine() = default;

ScoutEngine::~ScoutEngine()
{
    // Whatever is still parked belongs to us at teardown.
    for (auto& s : slots_)
    {
        delete s.active;
        delete s.pending.exchange (nullptr);
        delete s.retired.exchange (nullptr);
    }
}

void ScoutEngine::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 44100.0;
    reset();
}

void ScoutEngine::reset()
{
    for (auto& v : voices_) v = Voice {};
    activeVoices_.store (0, std::memory_order_relaxed);
    for (auto& s : slots_) s.lastPlayhead.store (-1.0, std::memory_order_relaxed);
}

// ------------------------------------------------------------------ slot handoff
WavSample* ScoutEngine::setSlot (int slot, WavSample* sample)
{
    if (slot < 0 || slot >= kSlots) return sample;
    Slot& s = slots_[(size_t) slot];
    s.requested.store (sample, std::memory_order_release);
    // a sample superseded before the audio thread saw it goes back to the caller: nothing points into it
    return s.pending.exchange (sample, std::memory_order_acq_rel);
}

WavSample* ScoutEngine::takeRetired (int slot)
{
    if (slot < 0 || slot >= kSlots) return nullptr;
    return slots_[(size_t) slot].retired.exchange (nullptr, std::memory_order_acq_rel);
}

WavSample* ScoutEngine::clearSlot (int slot)
{
    if (slot < 0 || slot >= kSlots) return nullptr;
    Slot& s = slots_[(size_t) slot];
    s.requested.store (nullptr, std::memory_order_release);
    WavSample* pending = s.pending.exchange (nullptr, std::memory_order_acq_rel);   // never reached the audio thread
    s.clear.store (true, std::memory_order_release);
    return pending;
}

void ScoutEngine::forgetAll()
{
    for (auto& v : voices_) v = Voice {};
    for (auto& s : slots_)
    {
        s.active = nullptr;
        s.pending.store (nullptr, std::memory_order_release);
        s.retired.store (nullptr, std::memory_order_release);
        s.requested.store (nullptr, std::memory_order_release);
        s.clear.store (false, std::memory_order_release);
    }
    activeVoices_.store (0, std::memory_order_relaxed);
}

void ScoutEngine::consumePending (Slot& s)
{
    const int idx = (int) (&s - slots_.data());
    if (s.clear.load (std::memory_order_acquire))
    {
        // same one-retiree rule as a swap: wait for the collector if it is behind
        if (s.active != nullptr && s.retired.load (std::memory_order_acquire) != nullptr) return;
        s.clear.store (false, std::memory_order_release);
        for (auto& v : voices_) if (v.slot == idx) v = Voice {};
        s.lastPlayhead.store (-1.0, std::memory_order_relaxed);
        s.lastNote.store (-1, std::memory_order_relaxed);
        if (s.active != nullptr) s.retired.store (s.active, std::memory_order_release);
        s.active = nullptr;
    }
    if (s.pending.load (std::memory_order_acquire) == nullptr) return;
    if (s.active != nullptr && s.retired.load (std::memory_order_acquire) != nullptr) return;
    WavSample* p = s.pending.exchange (nullptr, std::memory_order_acq_rel);
    if (p == nullptr) return;
    // only this slot's voices point into the old sample -- the others keep playing
    for (auto& v : voices_) if (v.slot == idx) v = Voice {};
    s.lastPlayhead.store (-1.0, std::memory_order_relaxed);
    WavSample* old = s.active;
    s.active = p;
    if (old != nullptr) s.retired.store (old, std::memory_order_release);
}

// ------------------------------------------------------------------ live state
void ScoutEngine::setSlotLoop (int slot, uint32_t startFrame, uint32_t endFrameInclusive, LoopKind kind)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[(size_t) slot];
    s.loop.store (((uint64_t) startFrame << 32) | (uint64_t) endFrameInclusive, std::memory_order_release);
    s.kind.store ((int) kind, std::memory_order_release);
}

void ScoutEngine::setSlotTuning (int slot, int rootKey, double fineCents)
{
    if (slot < 0 || slot >= kSlots) return;
    slots_[(size_t) slot].root.store (rootKey, std::memory_order_relaxed);
    slots_[(size_t) slot].cents.store (std::isfinite (fineCents) ? fineCents : 0.0, std::memory_order_relaxed);
}

void ScoutEngine::setSlotFades (int slot, double attackMs, double releaseMs)
{
    if (slot < 0 || slot >= kSlots) return;
    slots_[(size_t) slot].attackMs.store (attackMs, std::memory_order_relaxed);
    slots_[(size_t) slot].releaseMs.store (releaseMs, std::memory_order_relaxed);
}

void ScoutEngine::setKeyboard (KbMode mode, int splitNote, int toggleOn)
{
    kbMode_.store ((int) mode, std::memory_order_relaxed);
    split_.store (splitNote, std::memory_order_relaxed);
    toggleOn_.store (toggleOn != 0 ? 1 : 0, std::memory_order_relaxed);
}

int ScoutEngine::routeSlot (KbMode mode, int splitNote, int toggleOn, int note, bool haveA, bool haveB)
{
    int want = kSlotA;
    switch (mode)
    {
        case KbMode::A:      want = kSlotA; break;
        case KbMode::B:      want = kSlotB; break;
        case KbMode::Split:  want = note >= splitNote ? kSlotB : kSlotA; break;
        case KbMode::Toggle: want = toggleOn != 0 ? kSlotB : kSlotA; break;
    }
    // an empty target falls back to the loaded slot (never a silent keyboard)
    if (want == kSlotA && ! haveA) want = haveB ? kSlotB : -1;
    else if (want == kSlotB && ! haveB) want = haveA ? kSlotA : -1;
    return want;
}

// ------------------------------------------------------------------ voices
ScoutEngine::Voice* ScoutEngine::allocateVoice()
{
    Voice* oldest = nullptr;
    for (auto& v : voices_)
    {
        if (! v.active) return &v;
        if (oldest == nullptr || v.startOrder < oldest->startOrder) oldest = &v;
    }
    return oldest;   // oldest-note stealing (spec)
}

void ScoutEngine::startVoice (int slot, int note, int velocity)
{
    Slot& s = slots_[(size_t) slot];
    if (s.active == nullptr || s.active->frames == 0) return;
    Voice* vp = allocateVoice();
    if (vp == nullptr) return;
    Voice& v = *vp;
    v = Voice {};
    v.active = true;
    v.slot = slot;
    v.note = note;
    v.startOrder = ++orderCounter_;
    // transpose from the root key + fine tune (equal temperament about the root, A4 = 440)
    const double root = (double) s.root.load (std::memory_order_relaxed) + s.cents.load (std::memory_order_relaxed) / 100.0;
    double baseStep = std::pow (2.0, ((double) note - root) / 12.0) * (double) s.active->sampleRate / sampleRate_;
    // the last line of defence against a hostile rate/root: the step itself is clamped
    if (! std::isfinite (baseStep) || baseStep <= 0.0) baseStep = 1.0;
    v.baseStep = std::max (1.0 / 256.0, std::min (256.0, baseStep));
    double step = v.baseStep * std::pow (2.0, bendSemis_ / 12.0);
    if (! std::isfinite (step) || step <= 0.0) step = v.baseStep;
    v.step = std::max (1.0 / 256.0, std::min (256.0, step));
    v.gain = std::sqrt (std::max (1, std::min (127, velocity)) / 127.0f);   // sqrt velocity curve (D-4)
    v.dir = 1;
    // audition variant: LOOP-ONLY starts inside the loop
    const uint64_t packed = s.loop.load (std::memory_order_acquire);
    const double lStart = (double) (uint32_t) (packed >> 32);
    v.pos = mode_ == PlayMode::LoopOnly ? std::min (lStart, (double) s.active->frames - 1.0) : 0.0;
    const double attackMs = std::max (0.0, s.attackMs.load (std::memory_order_relaxed));
    if (attackMs > 0.0) { v.attack = 0.0f; v.attackStep = (float) (1.0 / (attackMs * 0.001 * sampleRate_)); }
    else                { v.attack = 1.0f; v.attackStep = 0.0f; }
    for (auto& o : voices_) if (o.slot == slot) o.isLatest = false;
    v.isLatest = true;
    s.lastNote.store (note, std::memory_order_relaxed);
    s.lastVel.store (velocity, std::memory_order_relaxed);
    s.noteSeq.fetch_add (1, std::memory_order_relaxed);
}

void ScoutEngine::noteOn (int note, int velocity)
{
    // noteOn() is an audio-thread API and a host (or the harness) can call it
    // before any process() has run -- consume handoffs here too (a no-op once
    // process() has run for the current samples).
    for (auto& s : slots_) consumePending (s);
    if (note < 0 || note > 127 || velocity <= 0) return;
    const int slot = routeSlot ((KbMode) kbMode_.load (std::memory_order_relaxed), split_.load (std::memory_order_relaxed),
                                toggleOn_.load (std::memory_order_relaxed), note,
                                slots_[kSlotA].active != nullptr, slots_[kSlotB].active != nullptr);
    if (slot < 0) return;
    startVoice (slot, note, velocity);
}

void ScoutEngine::noteOnSlot (int slot, int note, int velocity)
{
    if (slot < 0 || slot >= kSlots) return;
    consumePending (slots_[(size_t) slot]);
    if (note < 0 || note > 127 || velocity <= 0) return;
    startVoice (slot, note, velocity);
}

void ScoutEngine::beginRelease (Voice& v)
{
    const double ms = std::max (1.0, slots_[(size_t) std::max (0, v.slot)].releaseMs.load (std::memory_order_relaxed));
    v.releasing = true;
    v.fadeStep = (float) (1.0 / (ms * 0.001 * sampleRate_));
}

void ScoutEngine::noteOff (int note)
{
    for (auto& v : voices_)
        if (v.active && ! v.releasing && v.note == note)
            beginRelease (v);
}

void ScoutEngine::noteOffSlot (int slot, int note)
{
    for (auto& v : voices_)
        if (v.active && ! v.releasing && v.note == note && v.slot == slot)
            beginRelease (v);
}

void ScoutEngine::allNotesOff()
{
    for (auto& v : voices_)
        if (v.active && ! v.releasing) beginRelease (v);
}

void ScoutEngine::renderVoice (Voice& v, float* left, float* right, int numSamples, float gain)
{
    const Slot& s = slots_[(size_t) v.slot];
    const WavSample& w = *s.active;
    const float* L = w.L();
    const float* R = w.R();
    const double frames = (double) w.frames;
    const double lastFrame = frames - 1.0;
    const double lStart = std::min (s.curStart, lastFrame);
    const double lEnd   = std::min (s.curEnd, lastFrame);              // INCLUSIVE
    // LOOP-ONLY: an "off" loop still loops the marked region (v1 "no loop -> loop the whole sample")
    LoopKind kind = s.curKind;
    if (kind == LoopKind::Off && mode_ == PlayMode::LoopOnly) kind = LoopKind::Forward;
    const bool loop = kind != LoopKind::Off && lEnd > lStart;          // >= 2 frames in the loop
    const double len = lEnd - lStart;                                  // ping-pong half period
    const float g = gain * v.gain;
    double pos = v.pos;
    int dir = v.dir;
    const double step = v.step;
    float fade = v.fade;
    const float fadeStep = v.fadeStep;
    float attack = v.attack;
    const float attackStep = v.attackStep;

    for (int i = 0; i < numSamples; ++i)
    {
        if (loop)
        {
            if (kind == LoopKind::Forward)
            {
                // wrap [lStart, lEnd]: exclusive end is lEnd + 1 (fmod: one step no matter the overshoot)
                if (pos > lEnd)
                    pos = lStart + std::fmod (pos - lStart, len + 1.0);
            }
            else if ((dir > 0 && pos > lEnd) || (dir < 0 && pos < lStart))
            {
                // sample-accurate reflection at either marker
                if (dir > 0) { pos = 2.0 * lEnd - pos;   dir = -1; }
                else         { pos = 2.0 * lStart - pos; dir = +1; }
                if (pos < lStart || pos > lEnd)
                {
                    // hostile step longer than the loop: fold onto the 2*len triangle in one step
                    double t = std::fmod (pos - lStart, 2.0 * len);
                    if (t < 0.0) t += 2.0 * len;
                    if (t > len) { pos = lStart + 2.0 * len - t; dir = -1; }
                    else         { pos = lStart + t;             dir = +1; }
                }
            }
        }
        else if (pos >= lastFrame)
        {
            v.active = false;
            break;
        }
        if (! (pos >= 0.0) || pos >= frames) { v.active = false; break; }

        const uint32_t i0 = (uint32_t) pos;
        const double frac = pos - (double) i0;
        uint32_t i1 = i0 + 1;
        if (loop && kind == LoopKind::Forward && (double) i1 > lEnd) i1 = (uint32_t) lStart;   // seam interpolates into the loop start
        else if ((double) i1 >= frames) i1 = i0;
        const float sL = L[i0] + (float) frac * (L[i1] - L[i0]);
        const float sR = R[i0] + (float) frac * (R[i1] - R[i0]);

        if (attack < 1.0f) { attack += attackStep; if (attack > 1.0f) attack = 1.0f; }
        if (v.releasing)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { v.active = false; break; }
        }
        const float env = fade * attack * g;
        left[i]  += sL * env;
        right[i] += sR * env;
        pos += dir > 0 ? step : -step;
    }
    v.pos = pos;
    v.dir = dir;
    v.fade = fade;
    v.attack = attack;
}

void ScoutEngine::process (float* left, float* right, int numSamples, float gain)
{
    bool any = false;
    for (auto& s : slots_)
    {
        consumePending (s);
        any = any || s.active != nullptr;
        // per-block snapshot of the live loop edit (one 64-bit load: never torn)
        const uint64_t packed = s.loop.load (std::memory_order_acquire);
        s.curStart = (double) (uint32_t) (packed >> 32);
        s.curEnd   = (double) (uint32_t) (packed & 0xFFFFFFFFu);
        s.curKind  = (LoopKind) s.kind.load (std::memory_order_acquire);
    }
    if (! any || numSamples <= 0)
    {
        activeVoices_.store (0, std::memory_order_relaxed);
        for (auto& s : slots_) s.lastPlayhead.store (-1.0, std::memory_order_relaxed);
        return;
    }
    const double bendFactor = std::pow (2.0, bendSemis_ / 12.0);
    int count = 0;
    double latest[kSlots] = { -1.0, -1.0, -1.0 };
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        if (v.slot < 0 || v.slot >= kSlots || slots_[(size_t) v.slot].active == nullptr) { v.active = false; continue; }
        v.step = v.baseStep * bendFactor;
        renderVoice (v, left, right, numSamples, gain);
        if (v.active) { ++count; if (v.isLatest) latest[v.slot] = v.pos; }
    }
    activeVoices_.store (count, std::memory_order_relaxed);
    for (int i = 0; i < kSlots; ++i) slots_[(size_t) i].lastPlayhead.store (latest[i], std::memory_order_relaxed);
}

} // namespace sf2scout
