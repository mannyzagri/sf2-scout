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
    const double n = note + z.transpose + z.tuneCents / 100.0;
    const double adjusted = z.rootKey + (n - z.rootKey) * (z.keytrack / 100.0);
    v.baseStep = std::pow (2.0, (adjusted - z.rootKey) / 12.0) * (double) z.sampleRate / sampleRate_;
    v.step = v.baseStep * std::pow (2.0, bendSemis_ / 12.0);

    // Level: sqrt velocity curve, no SF2 attenuation (spec §2).
    v.gain = std::sqrt (std::max (1, std::min (127, velocity)) / 127.0f);
    // Linear pan from the region's pan (-1..+1); centre = unity on both sides,
    // so a mono sample's level is exactly its velocity gain (no SF2 attenuation).
    const float p = std::max (-1.0f, std::min (1.0f, z.pan));
    v.panL = 1.0f - std::max (0.0f, p);
    v.panR = 1.0f + std::min (0.0f, p);

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

void ScoutEngine::noteOn (int note, int velocity)
{
    consumePendingBank();   // audio-thread API; a note may precede the first block
    if (active_ == nullptr || note < 0 || note > 127 || velocity <= 0) return;
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
    lastPreset_.store (preset_, std::memory_order_relaxed);
    lastNoteNum_.store (note, std::memory_order_relaxed);
    lastVel_.store (velocity, std::memory_order_relaxed);
    lastZone_.store (zoneIndex, std::memory_order_relaxed);
    lastSeq_.fetch_add (1, std::memory_order_release);

    for (int i = 0; i < n; ++i)
        startVoice (*zones[i], note, velocity);
}

void ScoutEngine::noteOff (int note)
{
    const float step = (float) (1.0 / (kReleaseMs * 0.001 * sampleRate_));
    for (auto& v : voices_)
        if (v.active && ! v.releasing && v.note == note)
        {
            v.releasing = true;
            v.fadeStep = step;
        }
}

void ScoutEngine::allNotesOff()
{
    const float step = (float) (1.0 / (kReleaseMs * 0.001 * sampleRate_));
    for (auto& v : voices_)
        if (v.active && ! v.releasing) { v.releasing = true; v.fadeStep = step; }
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
            while (pos >= loopEnd) pos -= (loopEnd - v.loopStart);
        }
        else if (pos >= end - 1.0)
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
    if (active_ == nullptr || numSamples <= 0) { activeVoices_.store (0, std::memory_order_relaxed); return; }

    const double bendFactor = std::pow (2.0, bendSemis_ / 12.0);
    int count = 0;
    double latestPlayhead = -1.0;
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        v.step = v.baseStep * bendFactor;
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
}

LastNoteInfo ScoutEngine::lastNote() const
{
    LastNoteInfo i;
    // seq is written last by the audio thread (release); read it first (acquire)
    i.sequence    = lastSeq_.load (std::memory_order_acquire);
    i.presetIndex = lastPreset_.load (std::memory_order_relaxed);
    i.note        = lastNoteNum_.load (std::memory_order_relaxed);
    i.velocity    = lastVel_.load (std::memory_order_relaxed);
    i.zoneIndex   = lastZone_.load (std::memory_order_relaxed);
    return i;
}

} // namespace sf2scout
