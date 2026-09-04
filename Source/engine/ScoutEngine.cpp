#include "ScoutEngine.h"
#include <cmath>
#include <algorithm>

namespace sf2scout
{

ScoutEngine::ScoutEngine() = default;

ScoutEngine::~ScoutEngine()
{
    // Whatever is still parked belongs to us at teardown.
    delete active_;
    delete pending_.exchange (nullptr);
    delete retired_.exchange (nullptr);
    delete activeWav_;
    delete pendingWav_.exchange (nullptr);
    delete retiredWav_.exchange (nullptr);
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
    lastPlayhead_.store (-1.0, std::memory_order_relaxed);
    lastWavPlayhead_.store (-1.0, std::memory_order_relaxed);
}

// ------------------------------------------------------------------ Slot W handoff
void ScoutEngine::setWav (WavSample* wav)
{
    WavSample* prev = pendingWav_.exchange (wav, std::memory_order_acq_rel);
    delete prev;   // superseded before the audio thread saw it
}

WavSample* ScoutEngine::takeRetiredWav()
{
    return retiredWav_.exchange (nullptr, std::memory_order_acq_rel);
}

void ScoutEngine::consumePendingWav()
{
    if (pendingWav_.load (std::memory_order_acquire) == nullptr) return;
    if (activeWav_ != nullptr && retiredWav_.load (std::memory_order_acquire) != nullptr) return;
    WavSample* p = pendingWav_.exchange (nullptr, std::memory_order_acq_rel);
    if (p == nullptr) return;
    // only the WAV voices point into the old sample -- the SF2 voices keep playing
    for (auto& v : voices_) if (v.isWav) v = Voice {};
    lastWavPlayhead_.store (-1.0, std::memory_order_relaxed);
    WavSample* old = activeWav_;
    activeWav_ = p;
    if (old != nullptr) retiredWav_.store (old, std::memory_order_release);
}

void ScoutEngine::setWavLoop (uint32_t startFrame, uint32_t endFrameInclusive, WavLoopMode mode)
{
    wavLoop_.store (((uint64_t) startFrame << 32) | (uint64_t) endFrameInclusive, std::memory_order_release);
    wavLoopMode_.store ((int) mode, std::memory_order_release);
}

void ScoutEngine::setWavTuning (int rootKey, double fineCents)
{
    wavRoot_.store (rootKey, std::memory_order_relaxed);
    wavCents_.store (fineCents, std::memory_order_relaxed);
}

void ScoutEngine::setWavFades (double attackMs, double releaseMs)
{
    wavAttackMs_.store (attackMs, std::memory_order_relaxed);
    wavReleaseMs_.store (releaseMs, std::memory_order_relaxed);
}

void ScoutEngine::setFocus (Focus f, int splitNote)
{
    focus_.store ((int) f, std::memory_order_relaxed);
    split_.store (splitNote, std::memory_order_relaxed);
}

void ScoutEngine::setBank (SoundFontBank* bank)
{
    requested_.store (bank, std::memory_order_release);
    // If the audio thread never ran since the last setBank, the previous
    // pending bank is simply superseded -- retire it here.
    SoundFontBank* prev = pending_.exchange (bank, std::memory_order_acq_rel);
    delete prev;   // never reached the audio thread, so nothing points into it
}

SoundFontBank* ScoutEngine::takeRetiredBank()
{
    return retired_.exchange (nullptr, std::memory_order_acq_rel);
}

void ScoutEngine::consumePendingBank()
{
    if (pending_.load (std::memory_order_acquire) == nullptr) return;
    // Only one retiree can be parked. If the owner thread has not collected
    // the previous one yet, keep playing the current bank and retry next block.
    if (active_ != nullptr && retired_.load (std::memory_order_acquire) != nullptr) return;
    SoundFontBank* p = pending_.exchange (nullptr, std::memory_order_acq_rel);
    if (p == nullptr) return;
    // Voices point into the old bank's pool: kill them before it goes away.
    for (auto& v : voices_) v = Voice {};
    activeVoices_.store (0, std::memory_order_relaxed);
    lastPlayhead_.store (-1.0, std::memory_order_relaxed);
    SoundFontBank* old = active_;
    active_ = p;
    if (old != nullptr)
        retired_.store (old, std::memory_order_release);
}

ScoutEngine::Voice* ScoutEngine::allocateVoice()
{
    Voice* oldest = nullptr;
    for (auto& v : voices_)
    {
        if (! v.active) return &v;
        if (oldest == nullptr || v.startOrder < oldest->startOrder) oldest = &v;
    }
    return oldest;   // oldest-note stealing (spec §2)
}

void ScoutEngine::startVoice (const Zone& z, int note, int velocity)
{
    Voice* vp = allocateVoice();
    if (vp == nullptr) return;
    Voice& v = *vp;
    v = Voice {};
    v.active = true;
    v.note = note;
    v.zone = &z;
    v.startOrder = ++orderCounter_;

    // Pitch: mirror TSF's calc (transpose + tune, keytrack scaling about root).
    // F1 (BLOCKER): z.transpose/z.keytrack/z.tuneCents are already clamped at
    // load time (SoundFontBank::load), but keytrack can still amplify an
    // in-range note+transpose deviation into an astronomical exponent (e.g.
    // keytrack=1200% with a +120 semitone transpose at note 127 -> 2^187).
    // std::pow of that either overflows to +inf or, after further arithmetic,
    // can go non-finite/non-positive -- either would spin renderVoice's read
    // pointer forever. Clamp defensively, after the fact, on the computed step
    // itself: this is the last line of defence, independent of what generator
    // values produced it.
    const double n = note + z.transpose + z.tuneCents / 100.0;
    const double adjusted = z.rootKey + (n - z.rootKey) * (z.keytrack / 100.0);
    double baseStep = std::pow (2.0, (adjusted - z.rootKey) / 12.0) * (double) z.sampleRate / sampleRate_;
    if (! std::isfinite (baseStep) || baseStep <= 0.0) baseStep = 1.0;
    baseStep = std::max (1.0 / 256.0, std::min (256.0, baseStep));
    v.baseStep = baseStep;
    double step = v.baseStep * std::pow (2.0, bendSemis_ / 12.0);
    if (! std::isfinite (step) || step <= 0.0) step = v.baseStep;
    v.step = std::max (1.0 / 256.0, std::min (256.0, step));

    // Level: sqrt velocity curve, no SF2 attenuation (spec §2).
    v.gain = std::sqrt (std::max (1, std::min (127, velocity)) / 127.0f);
    // Linear pan from the region's pan (-1..+1); centre = unity on both sides,
    // so a mono sample's level is exactly its velocity gain (no SF2 attenuation).
    const float p = std::max (-1.0f, std::min (1.0f, z.pan));
    v.panL = 1.0f - std::max (0.0f, p);
    v.panR = 1.0f + std::min (0.0f, p);
    // F11: an untouched (pan==0) stereo half is authored to sit hard left/right,
    // not centre -- SF2's own linked-sample convention (sampleType 4 = left half,
    // 2 = right half). Only a region that never set a pan generator gets this;
    // an explicit pan (z.pan != 0) always wins via the general case above.
    if (z.pan == 0.0f && z.isStereoHalf())
    {
        if (z.sampleType == 4)      { v.panL = 1.0f; v.panR = 0.0f; }   // left half
        else if (z.sampleType == 2) { v.panL = 0.0f; v.panR = 1.0f; }   // right half
    }

    v.playEnd = (double) z.playEnd;
    const bool hasLoop = z.hasLoop();
    if (mode_ == PlayMode::LoopOnly)
    {
        if (hasLoop) { v.loopStart = z.loopStart; v.loopEnd = z.loopEnd; }
        else         { v.loopStart = z.playStart; v.loopEnd = z.playEnd; }   // whole sample
        v.loop = v.loopEnd > v.loopStart + 1;
        v.pos  = v.loopStart;
    }
    else
    {
        v.loop = hasLoop;
        v.loopStart = z.loopStart; v.loopEnd = z.loopEnd;
        v.pos  = z.playStart;
    }
    v.fade = 1.0f;
    v.fadeStep = 0.0f;
    for (auto& o : voices_) o.isLatest = false;
    v.isLatest = true;
}

void ScoutEngine::startWavVoice (int note, int velocity)
{
    Voice* vp = allocateVoice();
    if (vp == nullptr) return;
    Voice& v = *vp;
    v = Voice {};
    v.active = true;
    v.isWav = true;
    v.note = note;
    v.startOrder = ++orderCounter_;
    // transpose from the root key + fine tune (equal temperament about the root, A4 = 440)
    const double root  = (double) wavRoot_.load (std::memory_order_relaxed) + wavCents_.load (std::memory_order_relaxed) / 100.0;
    double baseStep = std::pow (2.0, ((double) note - root) / 12.0) * (double) activeWav_->sampleRate / sampleRate_;
    if (! std::isfinite (baseStep) || baseStep <= 0.0) baseStep = 1.0;
    v.baseStep = std::max (1.0 / 256.0, std::min (256.0, baseStep));
    double step = v.baseStep * std::pow (2.0, bendSemis_ / 12.0);
    if (! std::isfinite (step) || step <= 0.0) step = v.baseStep;
    v.step = std::max (1.0 / 256.0, std::min (256.0, step));
    v.gain = std::sqrt (std::max (1, std::min (127, velocity)) / 127.0f);
    v.panL = v.panR = 1.0f;
    v.pos = 0.0;
    v.dir = 1;
    const double attackMs = std::max (0.0, wavAttackMs_.load (std::memory_order_relaxed));
    if (attackMs > 0.0) { v.attack = 0.0f; v.attackStep = (float) (1.0 / (attackMs * 0.001 * sampleRate_)); }
    else                { v.attack = 1.0f; v.attackStep = 0.0f; }
    for (auto& o : voices_) o.isLatest = false;
    v.isLatest = true;
    lastWavNote_.store (note, std::memory_order_relaxed);
}

void ScoutEngine::noteOn (int note, int velocity)
{
    // F8 (NIT): kept deliberately. noteOn() is an audio-thread API and the
    // harness (and a real host feeding MIDI slightly ahead of the first
    // process() call) can call it before any process() has run, so `active_`
    // would otherwise still be null. Once process() has run at least once for
    // this bank, pending_ is already null and this is a single relaxed atomic
    // load that returns immediately -- a no-op, not a duplicate consume.
    consumePendingBank();
    consumePendingWav();
    if (note < 0 || note > 127 || velocity <= 0) return;
    if (routesToWav ((Focus) focus_.load (std::memory_order_relaxed), split_.load (std::memory_order_relaxed), note))
    {
        if (activeWav_ != nullptr) startWavVoice (note, velocity);
        return;
    }
    if (active_ == nullptr) return;
    const Zone* zones[8];
    const int n = active_->findZones (preset_, note, velocity, zones, 8);

    // publish readout first so a UI poll after this block sees the note even
    // if the zone had no playable sample
    const Zone* described = n > 0 ? zones[0] : active_->zoneForKey (preset_, note, velocity);
    int zoneIndex = -1;
    if (described != nullptr)
    {
        const auto& zs = active_->presets()[(size_t) preset_].zones;
        zoneIndex = (int) (described - zs.data());
    }
    // F5 seqlock write side: odd while the four fields below are in flight,
    // even once they're all consistent. See the comment on lastNote()/lastSeq_.
    lastSeq_.fetch_add (1, std::memory_order_acq_rel);   // -> odd: write in progress
    lastPreset_.store (preset_, std::memory_order_relaxed);
    lastNoteNum_.store (note, std::memory_order_relaxed);
    lastVel_.store (velocity, std::memory_order_relaxed);
    lastZone_.store (zoneIndex, std::memory_order_relaxed);
    lastSeq_.fetch_add (1, std::memory_order_release);   // -> even: write complete

    for (int i = 0; i < n; ++i)
        startVoice (*zones[i], note, velocity);
}

void ScoutEngine::beginRelease (Voice& v)
{
    // SF2 voices: the fixed protective fade (spec §4). WAV voices: the RELEASE
    // knob -- the loop keeps cycling underneath the fade either way.
    const double ms = v.isWav ? std::max (1.0, wavReleaseMs_.load (std::memory_order_relaxed)) : kReleaseMs;
    v.releasing = true;
    v.fadeStep = (float) (1.0 / (ms * 0.001 * sampleRate_));
}

void ScoutEngine::noteOff (int note)
{
    for (auto& v : voices_)
        if (v.active && ! v.releasing && v.note == note)
            beginRelease (v);
}

void ScoutEngine::allNotesOff()
{
    for (auto& v : voices_)
        if (v.active && ! v.releasing) beginRelease (v);
}

void ScoutEngine::renderWavVoice (Voice& v, float* left, float* right, int numSamples, float gain)
{
    const WavSample& w = *activeWav_;
    const float* L = w.L();
    const float* R = w.R();
    const double frames = (double) w.frames;
    const double lastFrame = frames - 1.0;
    const double lStart = std::min (curLoopStart_, lastFrame);
    const double lEnd   = std::min (curLoopEnd_, lastFrame);          // INCLUSIVE
    const WavLoopMode mode = curLoopMode_;
    const bool loop = mode != WavLoopMode::Off && lEnd > lStart;       // >= 2 frames in the loop
    const double len = lEnd - lStart;                                  // ping-pong half period
    const float gL = gain * v.gain * v.panL;
    const float gR = gain * v.gain * v.panR;
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
            if (mode == WavLoopMode::Forward)
            {
                // wrap [lStart, lEnd]: exclusive end is lEnd + 1 (fmod: one step no matter the overshoot)
                if (pos > lEnd)
                    pos = lStart + std::fmod (pos - lStart, len + 1.0);
            }
            else if ((dir > 0 && pos > lEnd) || (dir < 0 && pos < lStart))
            {
                // sample-accurate reflection at either marker: the overshoot past
                // the marker becomes the same distance back inside it
                if (dir > 0) { pos = 2.0 * lEnd - pos;   dir = -1; }
                else         { pos = 2.0 * lStart - pos; dir = +1; }
                if (pos < lStart || pos > lEnd)
                {
                    // hostile step longer than the loop: resolve in ONE step by
                    // folding onto the 2*len triangle measured upward from loopStart
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
        if (loop && mode == WavLoopMode::Forward && (double) i1 > lEnd) i1 = (uint32_t) lStart;   // seam interpolates into the loop start
        else if ((double) i1 >= frames) i1 = i0;
        const float sL = L[i0] + (float) frac * (L[i1] - L[i0]);
        const float sR = R[i0] + (float) frac * (R[i1] - R[i0]);

        if (attack < 1.0f) { attack += attackStep; if (attack > 1.0f) attack = 1.0f; }
        if (v.releasing)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { v.active = false; break; }
        }
        const float env = fade * attack;
        left[i]  += sL * env * gL;
        right[i] += sR * env * gR;
        pos += dir > 0 ? step : -step;
    }
    v.pos = pos;
    v.dir = dir;
    v.fade = fade;
    v.attack = attack;
}

void ScoutEngine::renderVoice (Voice& v, float* left, float* right, int numSamples, float gain)
{
    const float* pool = active_->samples();
    const double poolEnd = (double) active_->sampleCount();
    const double end = std::min (v.playEnd, poolEnd);
    const double loopEnd = std::min (v.loopEnd, end);
    const bool   loop = v.loop && loopEnd > v.loopStart + 1.0;
    const float  gL = gain * v.gain * v.panL;
    const float  gR = gain * v.gain * v.panR;
    double pos = v.pos;
    const double step = v.step;
    float fade = v.fade;
    const float fadeStep = v.fadeStep;

    for (int i = 0; i < numSamples; ++i)
    {
        if (loop)
        {
            // F1 (BLOCKER): loop length (loopEnd - v.loopStart) is guaranteed
            // > 1 here (the `loop` flag above requires it), so this fmod is
            // always a safe divisor. This replaces an unbounded
            // `while (pos >= loopEnd) pos -= length`: with a hostile/extreme
            // pitch (see startVoice), pos can be advanced by hundreds of loop
            // lengths in a single output sample, and that while loop would
            // iterate that many times PER SAMPLE -- spinning the audio thread.
            // fmod wraps it in one step regardless of how far pos overshot.
            if (pos >= loopEnd)
                pos = v.loopStart + std::fmod (pos - v.loopStart, loopEnd - v.loopStart);
        }
        else if (pos >= end - 1.0)
        {
            v.active = false;
            break;
        }
        // F1: belt-and-suspenders on the read pointer itself -- never let a
        // pathological pos (any NaN slipped in, or an edge the wrap above
        // didn't anticipate) turn into an out-of-bounds pool index.
        if (! (pos >= 0.0) || pos >= poolEnd)
        {
            v.active = false;
            break;
        }
        const uint32_t i0 = (uint32_t) pos;
        const double   frac = pos - (double) i0;
        // second tap: wrap inside the loop so the seam interpolates cleanly
        uint32_t i1 = i0 + 1;
        if (loop && (double) i1 >= loopEnd) i1 = (uint32_t) v.loopStart;
        else if ((double) i1 >= end) i1 = i0;
        const float s = pool[i0] + (float) frac * (pool[i1] - pool[i0]);

        if (v.releasing)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { v.active = false; break; }
        }
        const float out = s * fade;
        left[i]  += out * gL;
        right[i] += out * gR;
        pos += step;
    }
    v.pos = pos;
    v.fade = fade;
}

void ScoutEngine::process (float* left, float* right, int numSamples, float gain)
{
    consumePendingBank();
    consumePendingWav();
    if ((active_ == nullptr && activeWav_ == nullptr) || numSamples <= 0)
    {
        activeVoices_.store (0, std::memory_order_relaxed);
        return;
    }
    // per-block snapshot of the live loop edit (one 64-bit load: never torn)
    const uint64_t packed = wavLoop_.load (std::memory_order_acquire);
    curLoopStart_ = (double) (uint32_t) (packed >> 32);
    curLoopEnd_   = (double) (uint32_t) (packed & 0xFFFFFFFFu);
    curLoopMode_  = (WavLoopMode) wavLoopMode_.load (std::memory_order_acquire);

    const double bendFactor = std::pow (2.0, bendSemis_ / 12.0);
    int count = 0;
    double latestPlayhead = -1.0, latestWavPlayhead = -1.0;
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        v.step = v.baseStep * bendFactor;
        if (v.isWav)
        {
            if (activeWav_ == nullptr) { v.active = false; continue; }
            renderWavVoice (v, left, right, numSamples, gain);
            if (v.active) { ++count; if (v.isLatest) latestWavPlayhead = v.pos; }
            continue;
        }
        if (active_ == nullptr) { v.active = false; continue; }
        renderVoice (v, left, right, numSamples, gain);
        if (v.active)
        {
            ++count;
            if (v.isLatest && v.zone != nullptr)
                latestPlayhead = v.pos - (double) v.zone->sampleStart;
        }
    }
    activeVoices_.store (count, std::memory_order_relaxed);
    lastPlayhead_.store (latestPlayhead, std::memory_order_relaxed);
    lastWavPlayhead_.store (latestWavPlayhead, std::memory_order_relaxed);
}

LastNoteInfo ScoutEngine::lastNote() const
{
    // F5 seqlock read side: retry while the sequence is odd (writer mid-flight)
    // or changed between our first and last look (a write happened underneath
    // us) -- either way the four fields we just read could be torn across two
    // different note-ons. Bounded retries: never spin on the audio thread's
    // behalf.
    LastNoteInfo i;
    for (int tries = 0; tries < kSeqlockRetries; ++tries)
    {
        const uint32_t s1 = lastSeq_.load (std::memory_order_acquire);
        if (s1 & 1u) continue;                          // writer in progress
        i.sequence    = s1;
        i.presetIndex = lastPreset_.load (std::memory_order_relaxed);
        i.note        = lastNoteNum_.load (std::memory_order_relaxed);
        i.velocity    = lastVel_.load (std::memory_order_relaxed);
        i.zoneIndex   = lastZone_.load (std::memory_order_relaxed);
        const uint32_t s2 = lastSeq_.load (std::memory_order_acquire);
        if (s1 == s2) return i;                         // consistent snapshot
    }
    return i;   // best effort after kSeqlockRetries: still a real, if stale, snapshot
}

} // namespace sf2scout
