#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/NoteNames.h"
#include "engine/Assists.h"

namespace sf2scout
{

// ------------------------------------------------------------------ helpers
namespace
{
    // filesystem-safe piece of a file name: letters, digits, '-', '_' (spaces -> '_')
    juce::String safeName (juce::String s)
    {
        s = s.trim();
        juce::String out;
        for (auto c : s) out << (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_' ? juce::String::charToString (c) : juce::String ("_"));
        while (out.contains ("__")) out = out.replace ("__", "_");
        return out.trimCharactersAtStart ("_").trimCharactersAtEnd ("_");
    }
    juce::String two (int n) { return n < 10 ? "0" + juce::String (n) : juce::String (n); }
    juce::String S (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    const char* kindWord (int k) { return k == 1 ? "pingpong" : k == 2 ? "off" : "fwd"; }
}

SampleKey SampleKey::fromString (const juce::String& s)
{
    SampleKey k;
    auto parts = juce::StringArray::fromTokens (s, ":", "");
    if (parts.size() == 3) { k.src = parts[0].getIntValue(); k.a = parts[1].getIntValue(); k.b = parts[2].getIntValue(); }
    return k;
}

int Source::sampleCount() const
{
    switch (type)
    {
        case SourceType::Sf2:    return bank != nullptr ? bank->presetCount() : 0;
        case SourceType::Module: return module != nullptr ? module->nonEmptySampleCount() : 0;
        case SourceType::Wav:    return wav != nullptr ? 1 : 0;
        default:                 return 0;
    }
}

// ------------------------------------------------------------------ processor
juce::AudioProcessorValueTreeState::ParameterLayout ScoutProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamId::masterGain, 1 }, "Master",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.72f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamId::mode, 1 }, "Mode",
        juce::StringArray { "AS-AUTHORED", "LOOP-ONLY" }, 0));
    juce::StringArray channels { "OMNI" };
    for (int i = 1; i <= 16; ++i) channels.add ("CH " + juce::String (i));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamId::midiChannel, 1 }, "MIDI In", channels, 0));
    return layout;
}

ScoutProcessor::ScoutProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "SF2SCOUT", createLayout())
{
    gainParam_    = apvts_.getRawParameterValue (ParamId::masterGain);
    modeParam_    = apvts_.getRawParameterValue (ParamId::mode);
    channelParam_ = apvts_.getRawParameterValue (ParamId::midiChannel);
    // export folders are remembered across instances (spec: "remember last used")
    juce::PropertiesFile::Options o;
    // NOT "settings": that is the standalone wrapper's own file (window state, audio device, plugin state)
    o.applicationName = "SF2 Scout"; o.filenameSuffix = "exports"; o.folderName = "SF2 Scout";
    o.osxLibrarySubFolder = "Application Support";
    settings_ = std::make_unique<juce::PropertiesFile> (o);
    export_.folder = settings_->getValue ("exportFolder");
    export_.referenceFolder = settings_->getValue ("referenceFolder");
    export_.romFolder = settings_->getValue ("romFolder");
    startTimerHz (30);   // retired-sample collector + batch export ticks
}

ScoutProcessor::~ScoutProcessor()
{
    stopTimer();
    // every sample the engine points at is owned by decoded_ / engineHeld_ (shared_ptr):
    // the engine must forget them, not delete them, before those members go away
    engine_.forgetAll();
    engineHeld_.clear();
}

void ScoutProcessor::prepareToPlay (double sampleRate, int)
{
    engine_.prepare (sampleRate);
    gainSmoother_.reset (sampleRate, 0.02);
    gainSmoother_.setCurrentAndTargetValue (gainParam_->load());
}

void ScoutProcessor::releaseResources() {}

bool ScoutProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ScoutProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    engine_.setMode ((PlayMode) (int) modeParam_->load());
    const int channelFilter = (int) channelParam_->load();   // 0 = omni
    if (channelFilter != lastChannelFilter_)
    {
        engine_.allNotesOff();
        lastChannelFilter_ = channelFilter;
    }

    // UI audition queue
    {
        int start1, size1, start2, size2;
        uiNoteFifo_.prepareToRead (64, start1, size1, start2, size2);
        auto handle = [this] (int idx)
        {
            const UiNote& n = uiNotes_[(size_t) idx];
            if (n.slot < 0) { if (n.on) engine_.noteOn (n.note, n.velocity); else engine_.noteOff (n.note); }
            else            { if (n.on) engine_.noteOnSlot (n.slot, n.note, n.velocity); else engine_.noteOffSlot (n.slot, n.note); }
        };
        for (int i = 0; i < size1; ++i) handle (start1 + i);
        for (int i = 0; i < size2; ++i) handle (start2 + i);
        uiNoteFifo_.finishedRead (size1 + size2);
    }
    if (uiPanic_.exchange (false, std::memory_order_relaxed))
        engine_.allNotesOff();

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int n = end - pos;
            const int chunk = std::min (n, 32);
            const float g = gainSmoother_.getNextValue();
            gainSmoother_.skip (chunk - 1);
            engine_.process (left + pos, right + pos, chunk, g);
            pos += chunk;
        }
    };
    gainSmoother_.setTargetValue (gainParam_->load());

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int at = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderTo (at);
        if (msg.isNoteOn())
        {
            if (channelFilter == 0 || msg.getChannel() == channelFilter)
                engine_.noteOn (msg.getNoteNumber(), msg.getVelocity());
        }
        else if (msg.isNoteOff())  engine_.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine_.allNotesOff();
        else if (msg.isPitchWheel() && (channelFilter == 0 || msg.getChannel() == channelFilter))
            engine_.setPitchBend ((msg.getPitchWheelValue() - 8192) / 8192.0 * ScoutEngine::kBendRangeSt);
    }
    renderTo (numSamples);
}

void ScoutProcessor::timerCallback()
{
    collectRetired();
    if (batch_.running) batchTick();
}

void ScoutProcessor::collectRetired()
{
    for (int s = 0; s < kSlots; ++s)
        releaseHeld (engine_.takeRetired (s));                        // the shared_ptr drops its reference here
}

// ------------------------------------------------------------------ sources
bool ScoutProcessor::isModuleName (const juce::String& path)
{
    const juce::String ext = juce::File (path).getFileExtension().trimCharactersAtStart (".").toLowerCase();
    return ext.isNotEmpty() && ModuleSource::isSupportedExtension (ext.toStdString());
}

bool ScoutProcessor::isSourceName (const juce::String& path)
{
    return path.endsWithIgnoreCase (".sf2") || path.endsWithIgnoreCase (".wav") || isModuleName (path);
}

juce::String ScoutProcessor::loadFileBytes (const juce::File& file, juce::MemoryBlock& out) const
{
    if (! file.existsAsFile()) return "file not found";
    if (! file.loadFileAsData (out)) return "could not read the file";
    return {};
}

Source* ScoutProcessor::addSource (SourceType type, const juce::File& file)
{
    auto s = std::make_unique<Source>();
    s->id = nextSourceId_++;
    s->type = type;
    s->path = file.getFullPathName();
    s->fileName = file.getFileName();
    sources_.push_back (std::move (s));
    return sources_.back().get();
}

void ScoutProcessor::addErrorRow (const juce::File& file, const juce::String& error)
{
    // one error row per file: a retry replaces the previous reason
    for (auto& s : sources_)
        if (s->type == SourceType::Error && s->path == file.getFullPathName()) { s->error = error; bump(); return; }
    Source* s = addSource (SourceType::Error, file);
    s->error = error;
    bump();
}

juce::String ScoutProcessor::openSource (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD
    // already open: just select it
    for (auto& s : sources_)
        if (s->type != SourceType::Error && s->path == file.getFullPathName()) { selectSource (s->id); return {}; }
    juce::MemoryBlock bytes;
    juce::String err = loadFileBytes (file, bytes);
    if (err.isNotEmpty()) { addErrorRow (file, err); return file.getFileName() + ": " + err; }
    std::string e;
    const juce::String name = file.getFileName();
    if (name.endsWithIgnoreCase (".sf2"))
    {
        auto bank = SoundFontBank::load (bytes.getData(), bytes.getSize(), e);
        if (bank == nullptr) { addErrorRow (file, S (e)); return name + ": " + S (e); }
        bank->setFileName (name.toStdString());
        Source* s = addSource (SourceType::Sf2, file);
        s->bank = std::move (bank);
    }
    else if (isModuleName (file.getFullPathName()))
    {
        auto mod = ModuleSource::load (bytes.getData(), bytes.getSize(), name.toStdString(), e);
        if (mod == nullptr) { addErrorRow (file, S (e)); return name + ": " + S (e); }
        if (mod->firstNonEmpty() < 0) { addErrorRow (file, "module has no sample data"); return name + ": module has no sample data"; }
        Source* s = addSource (SourceType::Module, file);
        s->module = std::move (mod);
    }
    else if (name.endsWithIgnoreCase (".wav"))
    {
        auto wav = WavSample::load (bytes.getData(), bytes.getSize(), name.toStdString(), e);
        if (wav == nullptr) { addErrorRow (file, S (e)); return name + ": " + S (e); }
        Source* s = addSource (SourceType::Wav, file);
        s->wav = std::shared_ptr<WavSample> (wav.release());
    }
    else
    {
        addErrorRow (file, "not a recognised source (sf2, tracker module or wav)");
        return name + ": not a recognised source";
    }
    // remove a stale error row for the same file
    for (size_t i = 0; i < sources_.size(); ++i)
        if (sources_[i]->type == SourceType::Error && sources_[i]->path == file.getFullPathName()) { sources_.erase (sources_.begin() + (long) i); break; }
    selectSource (sources_.back()->id);
    return {};
}

void ScoutProcessor::unloadSource (int id)
{
    JUCE_ASSERT_MESSAGE_THREAD
    for (int s = 0; s < kSlots; ++s)
        if (slotKey_[(size_t) s].src == id) { clearSlot (s); slotKey_[(size_t) s] = {}; }
    if (cur_.src == id) { unlatch(); cur_ = {}; }
    for (auto it = decoded_.begin(); it != decoded_.end();) { if (it->first.src == id) it = decoded_.erase (it); else ++it; }
    for (auto it = edits_.begin(); it != edits_.end();) { if (it->first.src == id) it = edits_.erase (it); else ++it; }
    for (size_t i = 0; i < sources_.size(); ++i)
        if (sources_[i]->id == id) { sources_.erase (sources_.begin() + (long) i); break; }
    if (selSrc_ == id)
    {
        selSrc_ = -1;
        for (auto& s : sources_) if (s->type != SourceType::Error) selSrc_ = s->id;
        if (selSrc_ >= 0) selectSource (selSrc_);
    }
    bump();
}

const Source* ScoutProcessor::source (int id) const
{
    for (auto& s : sources_) if (s->id == id) return s.get();
    return nullptr;
}

Source* ScoutProcessor::source (int id)
{
    for (auto& s : sources_) if (s->id == id) return s.get();
    return nullptr;
}

std::vector<SampleKey> ScoutProcessor::keysOf (const Source& s, int preset, bool wholeSource) const
{
    std::vector<SampleKey> keys;
    switch (s.type)
    {
        case SourceType::Sf2:
            if (s.bank == nullptr) break;
            for (int p = 0; p < s.bank->presetCount(); ++p)
            {
                if (! wholeSource && p != preset) continue;
                const auto& zones = s.bank->presets()[(size_t) p].zones;
                for (int z = 0; z < (int) zones.size(); ++z) keys.push_back ({ s.id, p, z });
            }
            break;
        case SourceType::Module:
            if (s.module == nullptr) break;
            for (const auto& smp : s.module->samples()) if (! smp.isEmpty()) keys.push_back ({ s.id, smp.index, -1 });
            break;
        case SourceType::Wav:
            if (s.wav != nullptr) keys.push_back ({ s.id, -1, -1 });
            break;
        default: break;
    }
    return keys;
}

// ------------------------------------------------------------------ selection
void ScoutProcessor::selectSource (int id)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const Source* s = source (id);
    if (s == nullptr) return;
    selSrc_ = id;
    if (s->type == SourceType::Error) { bump(); return; }
    if (cur_.src != id)
    {
        auto keys = keysOf (*s, s->selPreset, false);
        if (! keys.empty()) (void) selectSample (keys.front());
    }
    bump();
}

void ScoutProcessor::selectPreset (int srcId, int preset)
{
    JUCE_ASSERT_MESSAGE_THREAD
    Source* s = source (srcId);
    if (s == nullptr || s->type != SourceType::Sf2 || s->bank == nullptr) return;
    s->selPreset = juce::jlimit (0, std::max (0, s->bank->presetCount() - 1), preset);
    selSrc_ = srcId;
    auto keys = keysOf (*s, s->selPreset, false);
    if (! keys.empty()) (void) selectSample (keys.front());
    bump();
}

juce::String ScoutProcessor::selectSample (const SampleKey& key)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const WavSample* w = decoded (key);
    if (w == nullptr) return "sample cannot be decoded";
    unlatch();
    cur_ = key;
    selSrc_ = key.src;
    if (Source* s = source (key.src)) if (s->type == SourceType::Sf2 && key.a >= 0) s->selPreset = key.a;
    installSlot (kSlotCur, key);
    // the export range defaults to the sample's loop (prototype select())
    const SampleEdits e = editsOrDefault (key);
    export_.rangeStart = e.loopStart;
    export_.rangeEnd = e.loopEnd + 1;
    bump();
    return {};
}

std::vector<ContentsRow> ScoutProcessor::contents (int srcId) const
{
    std::vector<ContentsRow> rows;
    const Source* s = source (srcId);
    if (s == nullptr) return rows;
    switch (s->type)
    {
        case SourceType::Sf2:
        {
            if (s->bank == nullptr || s->selPreset >= s->bank->presetCount()) break;
            const auto& zones = s->bank->presets()[(size_t) s->selPreset].zones;
            for (int z = 0; z < (int) zones.size(); ++z)
            {
                const Zone& zn = zones[(size_t) z];
                ContentsRow r;
                r.key = { s->id, s->selPreset, z };
                r.index = z + 1;
                r.name = S (zn.sampleName);
                r.bits = 16; r.channels = 1; r.frames = zn.sampleLength(); r.rate = zn.sampleRate;
                r.loopKind = zn.hasLoop() ? 0 : 2;
                r.lo = zn.lokey; r.hi = zn.hikey;
                rows.push_back (r);
            }
            break;
        }
        case SourceType::Module:
        {
            if (s->module == nullptr) break;
            for (const auto& smp : s->module->samples())
            {
                if (smp.isEmpty()) continue;
                ContentsRow r;
                r.key = { s->id, smp.index, -1 };
                r.index = smp.index;
                r.name = smp.name.empty() ? "(sample " + juce::String (smp.index) + ")" : S (smp.name);
                r.bits = smp.bits; r.channels = smp.channels; r.frames = smp.frames; r.rate = smp.sampleRate;
                r.loopKind = smp.hasSustain ? 3 : (smp.hasLoop ? (smp.pingPong ? 1 : 0) : 2);
                rows.push_back (r);
            }
            break;
        }
        case SourceType::Wav:
        {
            if (s->wav == nullptr) break;
            ContentsRow r;
            r.key = { s->id, -1, -1 };
            r.index = 1;
            r.name = juce::File (s->path).getFileNameWithoutExtension();
            r.bits = s->wav->bitsPerSample; r.channels = s->wav->channels; r.frames = s->wav->frames; r.rate = s->wav->sampleRate;
            r.loopKind = s->wav->hasSmpl && s->wav->loopEnd > s->wav->loopStart ? (s->wav->loopType == 1 ? 1 : 0) : 2;
            rows.push_back (r);
            break;
        }
        default: break;
    }
    return rows;
}

juce::String ScoutProcessor::contentsTitle (int srcId, juce::String& kindLabel) const
{
    const Source* s = source (srcId);
    kindLabel = "SAMPLES";
    if (s == nullptr) return juce::String::fromUTF8 ("\xE2\x80\x94");
    switch (s->type)
    {
        case SourceType::Sf2:
        {
            kindLabel = "ZONES OF PRESET";
            if (s->bank == nullptr || s->selPreset >= s->bank->presetCount()) return s->fileName;
            const Preset& p = s->bank->presets()[(size_t) s->selPreset];
            return juce::String (p.bank).paddedLeft ('0', 3) + ":" + juce::String (p.program).paddedLeft ('0', 3) + "  " + S (p.name);
        }
        case SourceType::Module:
            kindLabel = typeBadge (*s) + " SAMPLES " + juce::String::fromUTF8 ("\xC2\xB7") + " " + juce::String (s->module != nullptr ? s->module->nonEmptySampleCount() : 0);
            return s->fileName;
        case SourceType::Wav:
            kindLabel = "WAV SAMPLE";
            return s->fileName;
        default:
            return s->fileName;
    }
}

juce::String ScoutProcessor::stepSample (int delta)
{
    const Source* s = source (selSrc_);
    if (s == nullptr) return "no source selected";
    auto keys = keysOf (*s, s->selPreset, false);
    if (keys.empty()) return "nothing to step through";
    int pos = -1;
    for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == cur_) pos = (int) i;
    if (pos < 0) return selectSample (keys.front());
    const int n = (int) keys.size();
    return selectSample (keys[(size_t) (((pos + delta) % n + n) % n)]);
}

// ------------------------------------------------------------------ slots + keyboard
juce::String ScoutProcessor::assignSlot (int slot, const SampleKey& key)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (slot != kSlotA && slot != kSlotB) return "no such slot";
    if (decoded (key) == nullptr) return "sample cannot be decoded";
    slotKey_[(size_t) slot] = key;
    installSlot (slot, key);
    bump();
    return {};
}

void ScoutProcessor::setKbMode (KbMode m)     { kbMode_ = m; pushKeyboard(); bump(); }
void ScoutProcessor::setSplitNote (int n)     { splitNote_ = juce::jlimit (0, 127, n); pushKeyboard(); bump(); }
void ScoutProcessor::swapToggle()             { kbMode_ = KbMode::Toggle; toggleOn_ ^= 1; pushKeyboard(); bump(); }
void ScoutProcessor::pushKeyboard()           { engine_.setKeyboard (kbMode_, splitNote_, toggleOn_); }

bool ScoutProcessor::slotLive (int slot) const
{
    switch (kbMode_)
    {
        case KbMode::A:      return slot == kSlotA;
        case KbMode::B:      return slot == kSlotB;
        case KbMode::Split:  return slot == kSlotA || slot == kSlotB;
        case KbMode::Toggle: return slot == (toggleOn_ ? kSlotB : kSlotA);
    }
    return false;
}

void ScoutProcessor::installSlot (int slot, const SampleKey& key)
{
    auto it = decoded_.find (key);
    if (it == decoded_.end()) { clearSlot (slot); return; }
    std::shared_ptr<WavSample> w = it->second;
    const SampleEdits e = editsOrDefault (key);
    pushEdits (slot, e);
    collectRetired();
    auto& held = engineHeld_[w.get()];
    held.first = w; held.second += 1;
    // a sample still pending from an earlier install (the audio thread never saw it) comes straight back
    releaseHeld (engine_.setSlot (slot, w.get()));
}

void ScoutProcessor::clearSlot (int slot)
{
    collectRetired();
    releaseHeld (engine_.clearSlot (slot));
}

void ScoutProcessor::releaseHeld (WavSample* p)
{
    if (p == nullptr) return;
    auto it = engineHeld_.find (p);
    if (it == engineHeld_.end()) { delete p; return; }         // not ours to keep (should not happen)
    if (--it->second.second <= 0) engineHeld_.erase (it);
}

void ScoutProcessor::pushEdits (int slot, const SampleEdits& e)
{
    engine_.setSlotLoop (slot, e.loopStart, e.loopEnd, (LoopKind) juce::jlimit (0, 2, e.loopKind));
    engine_.setSlotTuning (slot, e.rootKey, e.fineCents);
    engine_.setSlotFades (slot, e.attackMs, e.releaseMs);
}

void ScoutProcessor::unlatch()
{
    if (latchedNote_ >= 0) { auditionSlot (kSlotCur, latchedNote_, false); latchedNote_ = -1; }
}

// ------------------------------------------------------------------ samples
std::unique_ptr<WavSample> ScoutProcessor::decodeFresh (const SampleKey& key, std::string& error) const
{
    const Source* s = source (key.src);
    if (s == nullptr) { error = "no such source"; return nullptr; }
    switch (s->type)
    {
        case SourceType::Sf2:
        {
            if (s->bank == nullptr || key.a < 0 || key.a >= s->bank->presetCount()) { error = "no such preset"; return nullptr; }
            const auto& zones = s->bank->presets()[(size_t) key.a].zones;
            if (key.b < 0 || key.b >= (int) zones.size()) { error = "no such zone"; return nullptr; }
            auto w = s->bank->decodeZone (zones[(size_t) key.b]);
            if (w == nullptr || w->frames < 2) { error = "zone has no sample data"; return nullptr; }
            return w;
        }
        case SourceType::Module:
            if (s->module == nullptr) { error = "no module"; return nullptr; }
            return s->module->decode (key.a, error);
        case SourceType::Wav:
            error = "wav is not decoded";   // never reached: the WAV source's own sample is used directly
            return nullptr;
        default:
            error = "source failed to load";
            return nullptr;
    }
}

const WavSample* ScoutProcessor::decoded (const SampleKey& key)
{
    if (! key.valid()) return nullptr;
    auto it = decoded_.find (key);
    if (it != decoded_.end()) return it->second.get();
    const Source* s = source (key.src);
    if (s == nullptr) return nullptr;
    std::shared_ptr<WavSample> w;
    if (s->type == SourceType::Wav) w = s->wav;
    else
    {
        std::string err;
        auto fresh = decodeFresh (key, err);
        if (fresh == nullptr) return nullptr;
        w = std::shared_ptr<WavSample> (fresh.release());
    }
    if (w == nullptr) return nullptr;
    decoded_[key] = w;
    if (edits_.find (key) == edits_.end()) edits_[key] = defaultEdits (key, *w);
    return w.get();
}

SampleEdits ScoutProcessor::defaultEdits (const SampleKey& key, const WavSample& w) const
{
    SampleEdits e;
    e.loopStart = w.loopStart;
    e.loopEnd = w.frames > 0 ? std::min (w.loopEnd, w.frames - 1) : 0;
    const Source* s = source (key.src);
    int kind = 2;
    if (s != nullptr && s->type == SourceType::Module && s->module != nullptr && key.a >= 1 && key.a <= s->module->sampleCount())
    {
        const auto& info = s->module->samples()[(size_t) key.a - 1];
        if (info.hasSustain) { e.sourceKind = 3; kind = info.sustainPingPong ? 1 : 0; }
        else if (info.hasLoop) { kind = info.pingPong ? 1 : 0; e.sourceKind = kind; }
        else { kind = 2; e.sourceKind = 2; }
    }
    else
    {
        kind = (w.hasSmpl && w.loopEnd > w.loopStart) ? (w.loopType == 1 ? 1 : 0) : 2;
        e.sourceKind = kind;
    }
    e.loopKind = kind;
    e.rootKey = w.rootKey;
    e.fineCents = w.fineCents;
    e.attackMs = 0.0;
    e.releaseMs = 80.0;
    e.prefix = safeName (nameOf (key)).toUpperCase().substring (0, 8);
    if (e.prefix.isEmpty()) e.prefix = "SAMPLE";
    e.description = w.bextDescription.empty()
        ? "wav=" + (s != nullptr ? s->fileName : juce::String ("?")) + " " + juce::String (w.bitsPerSample) + (w.formatTag == 3 ? "f " : "-bit ")
          + juce::String ((int) w.sampleRate) + "Hz " + (w.isStereo() ? "stereo" : "mono") + " loop=" + juce::String ((int) e.loopStart) + "-" + juce::String ((int) e.loopEnd) + " " + kindWord (kind)
        : S (w.bextDescription);
    e.dirty = false;
    return e;
}

const SampleEdits* ScoutProcessor::edits (const SampleKey& key) const
{
    auto it = edits_.find (key);
    return it == edits_.end() ? nullptr : &it->second;
}

SampleEdits ScoutProcessor::editsOrDefault (const SampleKey& key)
{
    if (decoded (key) == nullptr) return {};
    return edits_[key];
}

void ScoutProcessor::setEdits (const SampleKey& key, const SampleEdits& in)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const WavSample* w = decoded (key);
    if (w == nullptr) return;
    SampleEdits n = in;
    const uint32_t last = w->frames > 0 ? w->frames - 1 : 0;
    n.loopStart = juce::jmin (n.loopStart, last);
    n.loopEnd   = juce::jlimit (n.loopStart, last, n.loopEnd);
    n.loopKind  = juce::jlimit (0, 2, n.loopKind);
    n.rootKey   = juce::jlimit (0, 127, n.rootKey);
    if (! std::isfinite (n.fineCents)) n.fineCents = 0.0;
    n.fineCents = juce::jlimit (-99.0, 99.0, n.fineCents);
    n.attackMs  = juce::jlimit (0.0, 500.0, n.attackMs);
    n.releaseMs = juce::jlimit (10.0, 5000.0, n.releaseMs);
    SampleEdits& cur = edits_[key];
    if (n.loopStart != cur.loopStart || n.loopEnd != cur.loopEnd || n.loopKind != cur.loopKind
        || n.rootKey != cur.rootKey || n.fineCents != cur.fineCents || n.description != cur.description)
        n.dirty = true;
    n.sourceKind = cur.sourceKind;
    // "(override)" once the user changed the loop kind (F/P/O, LOOP seg); the flag survives later edits
    n.loopOverride = cur.loopOverride || in.loopOverride || n.loopKind != cur.loopKind;
    cur = n;
    for (int s = 0; s < kSlots; ++s) if (slotKey_[(size_t) s] == key || (s == kSlotCur && cur_ == key)) pushEdits (s, n);
    bump();
}

juce::String ScoutProcessor::nameOf (const SampleKey& key) const
{
    const Source* s = source (key.src);
    if (s == nullptr) return {};
    switch (s->type)
    {
        case SourceType::Sf2:
            if (s->bank != nullptr && key.a >= 0 && key.a < s->bank->presetCount())
            {
                const auto& zones = s->bank->presets()[(size_t) key.a].zones;
                if (key.b >= 0 && key.b < (int) zones.size()) return S (zones[(size_t) key.b].sampleName);
            }
            return {};
        case SourceType::Module:
            if (s->module != nullptr && key.a >= 1 && key.a <= s->module->sampleCount())
            {
                const auto& info = s->module->samples()[(size_t) key.a - 1];
                return info.name.empty() ? "sample" + two (key.a) : S (info.name);
            }
            return {};
        case SourceType::Wav:
            return juce::File (s->path).getFileNameWithoutExtension();
        default: return {};
    }
}

int ScoutProcessor::indexOf (const SampleKey& key) const
{
    const Source* s = source (key.src);
    if (s == nullptr) return 1;
    if (s->type == SourceType::Sf2) return key.b + 1;
    if (s->type == SourceType::Module) return key.a;
    return 1;
}

juce::String ScoutProcessor::pathOf (const SampleKey& key) const
{
    const Source* s = source (key.src);
    if (s == nullptr) return juce::String::fromUTF8 ("\xE2\x80\x94");
    const juce::String arrow = juce::String::fromUTF8 (" \xE2\x96\xB8 ");
    switch (s->type)
    {
        case SourceType::Sf2:
        {
            juce::String preset;
            if (s->bank != nullptr && key.a >= 0 && key.a < s->bank->presetCount()) preset = S (s->bank->presets()[(size_t) key.a].name);
            return s->fileName + arrow + preset + arrow + nameOf (key);
        }
        case SourceType::Module: return s->fileName + arrow + two (key.a) + " " + nameOf (key);
        default:                 return s->fileName;
    }
}

SourceType ScoutProcessor::typeOf (const SampleKey& key) const
{
    const Source* s = source (key.src);
    return s != nullptr ? s->type : SourceType::Error;
}

juce::String ScoutProcessor::typeBadge (const Source& s) const
{
    switch (s.type)
    {
        case SourceType::Sf2:    return "SF2";
        case SourceType::Module: return s.module != nullptr ? S (s.module->formatType()).toUpperCase() : juce::String ("MOD");
        case SourceType::Wav:    return "WAV";
        default:                 return "ERR";
    }
}

juce::String ScoutProcessor::typeBadge (const SampleKey& key) const
{
    const Source* s = source (key.src);
    return s != nullptr ? typeBadge (*s) : juce::String::fromUTF8 ("\xE2\x80\x94");
}

bool ScoutProcessor::isWavSource (const SampleKey& key) const
{
    return typeOf (key) == SourceType::Wav;
}

// ------------------------------------------------------------------ audition
void ScoutProcessor::auditionSlot (int slot, int note, bool on, int velocity)
{
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { slot, note, velocity, on }; uiNoteFifo_.finishedWrite (1); }
    else if (! on) uiPanic_.store (true, std::memory_order_relaxed);   // a dropped note-off would loop forever
}

void ScoutProcessor::auditionKeyboard (int note, bool on, int velocity)
{
    auditionSlot (-1, note, on, velocity);
}

void ScoutProcessor::setLatched (bool on)
{
    if (! on) { unlatch(); bump(); return; }
    if (! cur_.valid() || decoded (cur_) == nullptr) return;
    unlatch();
    latchedNote_ = editsOrDefault (cur_).rootKey;
    auditionSlot (kSlotCur, latchedNote_, true);
    bump();
}

// ------------------------------------------------------------------ save / export
WavSaveSpec ScoutProcessor::saveSpecFor (const SampleKey& key, const WavSample&, const SampleEdits& e) const
{
    WavSaveSpec spec;
    spec.loopStart = e.loopStart;
    spec.loopEnd   = e.loopEnd;
    spec.loopType  = e.loopKind == 1 ? 1 : 0;
    spec.rootKey   = e.rootKey;
    spec.fineCents = e.fineCents;
    juce::String desc = e.description.trim();
    const juce::String stamp = "Scout v" SF2SCOUT_VERSION_STRING " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d")
        + " loop=" + juce::String ((int) spec.loopStart) + ".." + juce::String ((int) spec.loopEnd) + " " + kindWord (e.loopKind)
        + " root=" + juce::String (spec.rootKey) + (std::fabs (spec.fineCents) > 0.005 ? juce::String::formatted ("%+.1fc", spec.fineCents) : juce::String())
        + " att=" + juce::String ((int) e.attackMs) + "ms rel=" + juce::String ((int) e.releaseMs) + "ms";
    desc = desc.isEmpty() ? stamp : desc + " | " + stamp;
    (void) key;
    spec.description = desc.toStdString().substr (0, 255);
    return spec;
}

juce::String ScoutProcessor::provenance (const SampleKey& key, const WavSample& w, const SampleEdits& e) const
{
    return S (saveSpecFor (key, w, e).description);
}

ExportOptions ScoutProcessor::exportOptionsFor (const SampleKey&, bool range) const
{
    ExportOptions o;
    o.convert16Bit441Mono = export_.convert16;
    o.stereoFold = export_.fold;
    o.hasRange = range;
    o.rangeStart = export_.rangeStart;
    o.rangeEnd = export_.rangeEnd;
    o.originator = "Scout v2";
    const juce::Time now = juce::Time::getCurrentTime();
    o.originationDate = now.formatted ("%Y-%m-%d").toStdString();
    o.originationTime = now.formatted ("%H:%M:%S").toStdString();
    return o;
}

juce::String ScoutProcessor::writeBytes (const juce::File& target, const std::vector<uint8_t>& bytes) const
{
    if (target.getFullPathName().isEmpty()) return "no target";
    if (! target.getParentDirectory().exists() && ! target.getParentDirectory().createDirectory()) return "cannot create " + target.getParentDirectory().getFullPathName();
    juce::TemporaryFile tmp (target);
    {
        juce::FileOutputStream out (tmp.getFile());
        if (! out.openedOk() || ! out.write (bytes.data(), bytes.size())) return "could not write " + target.getFileName();
        out.flush();
    }
    if (! tmp.overwriteTargetFileWithTemporary()) return "could not replace " + target.getFileName();
    return {};
}

juce::String ScoutProcessor::saveWav (const SampleKey& key)
{
    JUCE_ASSERT_MESSAGE_THREAD
    Source* s = source (key.src);
    if (s == nullptr || s->type != SourceType::Wav || s->wav == nullptr)
        return typeBadge (key) + " is a read-only container " + juce::String::fromUTF8 ("\xE2\x80\x94") + " use EXPORT SAMPLE";
    const WavSample* w = decoded (key);
    if (w == nullptr) return "no WAV loaded";
    SampleEdits& e = edits_[key];
    const WavSaveSpec spec = saveSpecFor (key, *w, e);
    const juce::String err = writeBytes (juce::File (s->path), writeWav (*w, spec));
    if (err.isNotEmpty()) return err;
    // the in-memory sample now carries what the file carries (metadata only; audio untouched)
    WavSample* mw = s->wav.get();
    mw->hasSmpl = true; mw->loopStart = spec.loopStart; mw->loopEnd = spec.loopEnd; mw->loopType = spec.loopType;
    mw->rootKey = spec.rootKey; mw->fineCents = spec.fineCents; mw->rootFromFile = true; mw->bextDescription = spec.description;
    e.dirty = false;
    bump();
    return {};
}

juce::String ScoutProcessor::saveAs (const SampleKey& key, const juce::File& target)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const WavSample* w = decoded (key);
    if (w == nullptr) return "nothing selected";
    if (! target.hasFileExtension ("wav")) return "target must be a .wav";
    SampleEdits& e = edits_[key];
    const WavSaveSpec spec = saveSpecFor (key, *w, e);
    std::vector<uint8_t> bytes;
    if (isWavSource (key)) bytes = writeWav (*w, spec);                          // the user's own file: byte-identical audio
    else
    {
        ExportOptions o = exportOptionsFor (key, false);
        o.convert16Bit441Mono = false;                                            // SAVE AS keeps the native format
        ExportResult r = exportWav (*w, spec, o);
        if (! r.error.empty()) return S (r.error);
        bytes = std::move (r.bytes);
    }
    const juce::String err = writeBytes (target, bytes);
    if (err.isNotEmpty()) return err;
    if (isWavSource (key)) e.dirty = false;
    export_.folder = target.getParentDirectory().getFullPathName();
    settings_->setValue ("exportFolder", export_.folder);
    bump();
    return {};
}

juce::File ScoutProcessor::exportFolderOrDefault (const SampleKey& key) const
{
    if (export_.folder.isNotEmpty()) return juce::File (export_.folder);
    if (const Source* s = source (key.src)) if (s->path.isNotEmpty()) return juce::File (s->path).getParentDirectory();
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

juce::File ScoutProcessor::saveAsSuggestion (const SampleKey& key) const
{
    const SampleEdits* e = edits (key);
    const juce::String prefix = e != nullptr && e->prefix.isNotEmpty() ? e->prefix : juce::String ("SAMPLE");
    const int root = e != nullptr ? e->rootKey : 60;
    return exportFolderOrDefault (key).getChildFile (prefix + "_" + S (noteName (root)) + ".wav");
}

void ScoutProcessor::setExportSettings (const ExportSettings& in)
{
    ExportSettings n = in;
    n.quick = juce::jlimit (0, 2, n.quick);
    n.fold = juce::jlimit (0, 1, n.fold);
    n.scope = juce::jlimit (0, 1, n.scope);
    export_ = n;
    settings_->setValue ("exportFolder", export_.folder);
    settings_->setValue ("referenceFolder", export_.referenceFolder);
    settings_->setValue ("romFolder", export_.romFolder);
    bump();
}

juce::String ScoutProcessor::exportFileName (const SampleKey& key) const
{
    const Source* s = source (key.src);
    if (s == nullptr) return {};
    const SampleEdits* e = edits (key);
    const juce::String note = S (noteName (e != nullptr ? e->rootKey : 60));
    const juce::String stem = safeName (juce::File (s->path).getFileNameWithoutExtension());
    if (s->type == SourceType::Wav) return stem + "_" + note + ".wav";
    return stem + "_" + two (indexOf (key)) + "_" + safeName (nameOf (key)) + "_" + note + ".wav";
}

juce::String ScoutProcessor::exportFormatNote (const SampleKey& key) const
{
    if (export_.convert16) return juce::String::fromUTF8 ("resample + TPDF dither \xE2\x86\x92 44100 Hz 16-bit mono");
    const Source* s = source (key.src);
    if (s == nullptr || ! key.valid()) return "source rate & depth";
    auto it = decoded_.find (key);
    if (it == decoded_.end()) return "source rate & depth";
    const WavSample& w = *it->second;
    int srcBits = w.bitsPerSample;
    if (s->type == SourceType::Module && s->module != nullptr && key.a >= 1 && key.a <= s->module->sampleCount())
        srcBits = s->module->samples()[(size_t) key.a - 1].bits;
    juce::String bits = srcBits < 16 ? juce::String (srcBits) + juce::String::fromUTF8 ("\xE2\x86\x92") + "16-bit" : juce::String (w.bitsPerSample) + (w.formatTag == 3 ? "f" : "-bit");
    return "writes " + juce::String ((int) w.sampleRate) + " Hz " + bits + " " + (w.isStereo() ? juce::String ("stereo") + juce::String::fromUTF8 ("\xE2\x86\x92") + "mono" : juce::String ("mono"));
}

juce::String ScoutProcessor::exportSample (const SampleKey& key, juce::String& writtenPath)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const WavSample* w = decoded (key);
    if (w == nullptr) return "nothing selected";
    if (export_.folder.isEmpty()) return "choose an export folder first";
    const SampleEdits e = edits_[key];
    ExportResult r = exportWav (*w, saveSpecFor (key, *w, e), exportOptionsFor (key, false));
    if (! r.error.empty()) return S (r.error);
    const juce::File target = juce::File (export_.folder).getChildFile (exportFileName (key));
    const juce::String err = writeBytes (target, r.bytes);
    if (err.isNotEmpty()) return err;
    writtenPath = target.getFullPathName();
    return {};
}

juce::String ScoutProcessor::exportRange (const SampleKey& key, juce::String& writtenPath, bool& markersDropped)
{
    JUCE_ASSERT_MESSAGE_THREAD
    markersDropped = false;
    const WavSample* w = decoded (key);
    if (w == nullptr) return "nothing selected";
    if (export_.folder.isEmpty()) return "choose an export folder first";
    if (export_.rangeEnd <= export_.rangeStart) return "end must exceed start";
    const SampleEdits e = edits_[key];
    ExportResult r = exportWav (*w, saveSpecFor (key, *w, e), exportOptionsFor (key, true));
    if (! r.error.empty()) return S (r.error);
    markersDropped = ! r.loopKept;
    juce::String name = exportFileName (key);
    name = name.dropLastCharacters (4) + "_r" + juce::String ((int) export_.rangeStart) + "-" + juce::String ((int) export_.rangeEnd) + ".wav";
    const juce::File target = juce::File (export_.folder).getChildFile (name);
    const juce::String err = writeBytes (target, r.bytes);
    if (err.isNotEmpty()) return err;
    writtenPath = target.getFullPathName();
    return {};
}

juce::String ScoutProcessor::batchScopeLabel() const
{
    const Source* s = source (cur_.valid() ? cur_.src : selSrc_);
    if (s == nullptr) return "MODULE";
    return s->type == SourceType::Sf2 ? "PRESET" : s->type == SourceType::Wav ? "FILE" : "MODULE";
}

juce::String ScoutProcessor::startBatch()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (batch_.running) return "batch already running";
    const Source* s = source (cur_.valid() ? cur_.src : selSrc_);
    if (s == nullptr || s->type == SourceType::Error) return "no source selected";
    if (export_.folder.isEmpty()) return "choose an export folder first";
    batchQueue_ = keysOf (*s, cur_.valid() && cur_.src == s->id && s->type == SourceType::Sf2 ? cur_.a : s->selPreset, export_.scope == 1);
    if (batchQueue_.empty()) return "nothing to export";
    batch_ = BatchState {};
    batch_.running = true;
    batch_.total = (int) batchQueue_.size();
    bump();
    return {};
}

void ScoutProcessor::cancelBatch()
{
    if (! batch_.running) return;
    batch_.running = false;
    batch_.log.insert (0, "cancelled at " + juce::String (batch_.done) + " / " + juce::String (batch_.total));
    while (batch_.log.size() > 4) batch_.log.remove (batch_.log.size() - 1);
    batch_.current = "idle";
    bump();
}

void ScoutProcessor::batchTick()
{
    if (! batch_.running) return;
    if (batch_.done >= batch_.total)
    {
        batch_.running = false;
        batch_.lastComplete = true;
        batch_.current = "idle " + juce::String::fromUTF8 ("\xC2\xB7") + " last batch complete";
        batch_.log.insert (0, "done " + juce::String::fromUTF8 ("\xC2\xB7") + " " + juce::String (batch_.written) + " written "
                              + juce::String::fromUTF8 ("\xC2\xB7") + " " + juce::String (batch_.skipped) + " skipped");
        while (batch_.log.size() > 4) batch_.log.remove (batch_.log.size() - 1);
        bump();
        return;
    }
    const SampleKey key = batchQueue_[(size_t) batch_.done];
    batch_.current = "writing " + two (indexOf (key)) + " " + nameOf (key);
    juce::String line;
    const WavSample* w = decoded (key);
    if (w == nullptr) { line = "skipped " + nameOf (key) + " (cannot decode)"; ++batch_.skipped; }
    else
    {
        const juce::String name = exportFileName (key);
        const juce::File target = juce::File (export_.folder).getChildFile (name);
        if (export_.skipExisting && target.existsAsFile()) { line = "exists " + name; ++batch_.skipped; }
        else
        {
            const SampleEdits e = edits_[key];
            ExportResult r = exportWav (*w, saveSpecFor (key, *w, e), exportOptionsFor (key, false));
            const juce::String err = r.error.empty() ? writeBytes (target, r.bytes) : S (r.error);
            if (err.isNotEmpty()) { line = "FAILED " + name + ": " + err; ++batch_.skipped; }
            else { line = name; ++batch_.written; }
        }
    }
    batch_.log.insert (0, line);
    while (batch_.log.size() > 4) batch_.log.remove (batch_.log.size() - 1);
    ++batch_.done;
    bump();
}

// ------------------------------------------------------------------ metadata
std::vector<ScoutProcessor::MetaRow> ScoutProcessor::metadata (int srcId) const
{
    std::vector<MetaRow> rows;
    const Source* s = source (srcId);
    if (s == nullptr) { rows.push_back ({ "", "no source selected" }); return rows; }
    switch (s->type)
    {
        case SourceType::Sf2:
            if (s->bank != nullptr) for (const auto& kv : s->bank->info()) rows.push_back ({ S (kv.first), S (kv.second) });
            if (rows.empty()) rows.push_back ({ "", "no INFO list in this file" });
            break;
        case SourceType::Module:
            if (s->module != nullptr)
            {
                rows.push_back ({ "format", S (s->module->formatName()) + " (" + S (s->module->formatType()) + ")" });
                if (! s->module->madeWith().empty()) rows.push_back ({ "made", S (s->module->madeWith()) });
                rows.push_back ({ "title", S (s->module->title()) });
                if (! s->module->message().empty()) rows.push_back ({ "message", S (s->module->message()) });
                juce::String instr;
                for (const auto& n : s->module->instrumentNames()) if (! n.empty()) instr << (instr.isEmpty() ? "" : "\n") << S (n);
                rows.push_back ({ "instr", juce::String ((int) s->module->instrumentNames().size()) + " instruments " + juce::String::fromUTF8 ("\xC2\xB7") + " "
                                           + juce::String (s->module->nonEmptySampleCount()) + " samples" + (instr.isNotEmpty() ? "\n" + instr : juce::String()) });
            }
            break;
        case SourceType::Wav:
            if (s->wav != nullptr)
            {
                const WavSample& w = *s->wav;
                rows.push_back ({ "fmt", juce::String ((int) w.sampleRate) + " Hz " + juce::String (w.bitsPerSample) + (w.formatTag == 3 ? "f " : "-bit ") + (w.isStereo() ? "stereo" : "mono") });
                if (w.hasSmpl) rows.push_back ({ "smpl", "loop " + juce::String ((int) w.loopStart) + juce::String::fromUTF8 ("\xE2\x80\x93") + juce::String ((int) w.loopEnd) + " " + (w.loopType == 1 ? "pp" : "fwd") + " " + juce::String::fromUTF8 ("\xC2\xB7") + " unity " + juce::String (w.rootKey) });
                else rows.push_back ({ "smpl", "none (root from " + juce::String (w.rootFromFile ? "file name" : "default 60") + ")" });
                if (! w.bextDescription.empty()) rows.push_back ({ "bext", S (w.bextDescription) });
                for (const auto& c : w.chunks)
                {
                    const juce::String id (c.id, 4);
                    if (id == "fmt " || id == "data") continue;
                    rows.push_back ({ id.trim(), juce::String ((int) c.body.size()) + " bytes" });
                }
            }
            break;
        default:
            rows.push_back ({ "error", s->error });
            break;
    }
    return rows;
}

// ------------------------------------------------------------------ state
void ScoutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    // sources in order (error rows are not persisted); keys are stored by position
    juce::ValueTree srcs ("sources");
    std::map<int, int> idToIndex;
    for (auto& s : sources_)
    {
        if (s->type == SourceType::Error) continue;
        juce::ValueTree t ("source");
        t.setProperty ("path", s->path, nullptr);
        t.setProperty ("selPreset", s->selPreset, nullptr);
        t.setProperty ("expanded", s->expanded, nullptr);
        idToIndex[s->id] = srcs.getNumChildren();
        srcs.appendChild (t, nullptr);
    }
    auto keyStr = [&] (const SampleKey& k) -> juce::String
    {
        auto it = idToIndex.find (k.src);
        return it == idToIndex.end() ? juce::String() : SampleKey { it->second, k.a, k.b }.toString();
    };
    state.removeChild (state.getChildWithName ("sources"), nullptr);
    state.addChild (srcs, -1, nullptr);
    juce::ValueTree ed ("edits");
    for (const auto& kv : edits_)
    {
        const juce::String ks = keyStr (kv.first);
        if (ks.isEmpty()) continue;
        const SampleEdits& e = kv.second;
        juce::ValueTree t ("edit");
        t.setProperty ("key", ks, nullptr);
        t.setProperty ("loopStart", (int) e.loopStart, nullptr);
        t.setProperty ("loopEnd", (int) e.loopEnd, nullptr);
        t.setProperty ("loopKind", e.loopKind, nullptr);
        t.setProperty ("loopOverride", e.loopOverride, nullptr);
        t.setProperty ("root", e.rootKey, nullptr);
        t.setProperty ("cents", e.fineCents, nullptr);
        t.setProperty ("attackMs", e.attackMs, nullptr);
        t.setProperty ("releaseMs", e.releaseMs, nullptr);
        t.setProperty ("prefix", e.prefix, nullptr);
        t.setProperty ("description", e.description, nullptr);
        ed.appendChild (t, nullptr);
    }
    state.removeChild (state.getChildWithName ("edits"), nullptr);
    state.addChild (ed, -1, nullptr);
    state.setProperty ("selSrc", idToIndex.count (selSrc_) ? idToIndex[selSrc_] : -1, nullptr);
    state.setProperty ("cur", keyStr (cur_), nullptr);
    state.setProperty ("slotA", keyStr (slotKey_[kSlotA]), nullptr);
    state.setProperty ("slotB", keyStr (slotKey_[kSlotB]), nullptr);
    state.setProperty ("kbMode", (int) kbMode_, nullptr);
    state.setProperty ("splitNote", splitNote_, nullptr);
    state.setProperty ("toggleOn", toggleOn_, nullptr);
    state.setProperty ("exportFolder", export_.folder, nullptr);
    state.setProperty ("exportQuick", export_.quick, nullptr);
    state.setProperty ("exportConvert16", export_.convert16, nullptr);
    state.setProperty ("exportFold", export_.fold, nullptr);
    state.setProperty ("exportScope", export_.scope, nullptr);
    state.setProperty ("exportSkip", export_.skipExisting, nullptr);
    state.setProperty ("midiInputDevice", midiInputDevice_, nullptr);
    state.setProperty ("uiScale", uiScale_, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ScoutProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts_.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    pendingRestoreTree_ = state.createCopy();
    juce::ValueTree params = state.createCopy();
    params.removeChild (params.getChildWithName ("sources"), nullptr);
    params.removeChild (params.getChildWithName ("edits"), nullptr);
    apvts_.replaceState (params);
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        performPendingRestore();
    else
        triggerAsyncUpdate();
}

void ScoutProcessor::handleAsyncUpdate()
{
    performPendingRestore();
}

void ScoutProcessor::performPendingRestore()
{
    JUCE_ASSERT_MESSAGE_THREAD
    juce::ValueTree state = pendingRestoreTree_;
    pendingRestoreTree_ = {};
    if (! state.isValid()) return;
    // start from nothing
    unlatch();
    for (int s = 0; s < kSlots; ++s) { clearSlot (s); slotKey_[(size_t) s] = {}; }
    cur_ = {}; selSrc_ = -1;
    decoded_.clear(); edits_.clear();
    sources_.clear();
    std::vector<int> indexToId;
    juce::ValueTree srcs = state.getChildWithName ("sources");
    for (int i = 0; i < srcs.getNumChildren(); ++i)
    {
        juce::ValueTree t = srcs.getChild (i);
        const size_t before = sources_.size();
        (void) openSource (juce::File (t.getProperty ("path").toString()));
        int id = -1;
        if (sources_.size() > before) id = sources_.back()->id;
        indexToId.push_back (id);
        if (Source* s = source (id))
        {
            s->selPreset = (int) t.getProperty ("selPreset", s->selPreset);
            s->expanded = (bool) t.getProperty ("expanded", true);
        }
    }
    auto keyOf = [&] (const juce::String& s) -> SampleKey
    {
        SampleKey k = SampleKey::fromString (s);
        if (k.src < 0 || k.src >= (int) indexToId.size() || indexToId[(size_t) k.src] < 0) return {};
        const Source* src = source (indexToId[(size_t) k.src]);
        if (src == nullptr || src->type == SourceType::Error) return {};
        k.src = src->id;
        return k;
    };
    juce::ValueTree ed = state.getChildWithName ("edits");
    for (int i = 0; i < ed.getNumChildren(); ++i)
    {
        juce::ValueTree t = ed.getChild (i);
        const SampleKey k = keyOf (t.getProperty ("key").toString());
        if (! k.valid() || decoded (k) == nullptr) continue;
        SampleEdits e = edits_[k];
        e.loopStart = (uint32_t) juce::jmax (0, (int) t.getProperty ("loopStart", (int) e.loopStart));
        e.loopEnd   = (uint32_t) juce::jmax (0, (int) t.getProperty ("loopEnd", (int) e.loopEnd));
        e.loopKind  = (int) t.getProperty ("loopKind", e.loopKind);
        e.loopOverride = (bool) t.getProperty ("loopOverride", e.loopOverride);
        e.rootKey   = (int) t.getProperty ("root", e.rootKey);
        e.fineCents = (double) t.getProperty ("cents", e.fineCents);
        e.attackMs  = (double) t.getProperty ("attackMs", e.attackMs);
        e.releaseMs = (double) t.getProperty ("releaseMs", e.releaseMs);
        e.prefix    = t.getProperty ("prefix", e.prefix).toString();
        e.description = t.getProperty ("description", e.description).toString();
        setEdits (k, e);
        edits_[k].dirty = false;
    }
    kbMode_ = (KbMode) juce::jlimit (0, 3, (int) state.getProperty ("kbMode", 0));
    splitNote_ = juce::jlimit (0, 127, (int) state.getProperty ("splitNote", 60));
    toggleOn_ = (int) state.getProperty ("toggleOn", 0) ? 1 : 0;
    pushKeyboard();
    const SampleKey a = keyOf (state.getProperty ("slotA", "").toString());
    const SampleKey b = keyOf (state.getProperty ("slotB", "").toString());
    if (a.valid()) (void) assignSlot (kSlotA, a);
    if (b.valid()) (void) assignSlot (kSlotB, b);
    const int selIdx = (int) state.getProperty ("selSrc", -1);
    if (selIdx >= 0 && selIdx < (int) indexToId.size() && indexToId[(size_t) selIdx] >= 0) selSrc_ = indexToId[(size_t) selIdx];
    const SampleKey c = keyOf (state.getProperty ("cur", "").toString());
    if (c.valid()) (void) selectSample (c);
    else if (selSrc_ >= 0) selectSource (selSrc_);
    export_.folder = state.getProperty ("exportFolder", export_.folder).toString();
    export_.quick = (int) state.getProperty ("exportQuick", export_.quick);
    export_.convert16 = (bool) state.getProperty ("exportConvert16", export_.convert16);
    export_.fold = (int) state.getProperty ("exportFold", export_.fold);
    export_.scope = (int) state.getProperty ("exportScope", export_.scope);
    export_.skipExisting = (bool) state.getProperty ("exportSkip", export_.skipExisting);
    midiInputDevice_ = state.getProperty ("midiInputDevice", midiInputDevice_).toString();
    uiScale_ = juce::jlimit (0.75, 1.5, (double) state.getProperty ("uiScale", uiScale_));
    bump();
}

juce::AudioProcessorEditor* ScoutProcessor::createEditor()
{
    return new ScoutEditor (*this);
}

} // namespace sf2scout

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sf2scout::ScoutProcessor();
}
