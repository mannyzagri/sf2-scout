#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "engine/NoteNames.h"

namespace sf2scout
{

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
    startTimerHz (10);   // retired-bank collector; nothing else
}

ScoutProcessor::~ScoutProcessor()
{
    stopTimer();
    delete engine_.takeRetiredBank();
    delete engine_.takeRetiredWav();
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
    if (buffer.getNumChannels() < 2) return;   // F12: this tool is stereo-only; nothing else to render
    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    engine_.setMode ((PlayMode) (int) modeParam_->load());
    engine_.setPreset (presetIndex_.load (std::memory_order_relaxed));
    const int channelFilter = (int) channelParam_->load();   // 0 = omni

    // F9: a MIDI-channel change can leave notes from the old channel held forever
    // (their note-offs would then be filtered out below) -- panic-clear instead.
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
            if (! n.on)       engine_.noteOff (n.note);
            else if (n.wav)   engine_.noteOnWav (n.note, n.velocity);
            else              engine_.noteOn (n.note, n.velocity);
        };
        for (int i = 0; i < size1; ++i) handle (start1 + i);
        for (int i = 0; i < size2; ++i) handle (start2 + i);
        uiNoteFifo_.finishedRead (size1 + size2);
    }
    // F10: auditionRelease refuses to drop a note-off; if the FIFO was full it
    // flags a panic instead, which we resolve with a hard all-notes-off.
    if (uiPanic_.exchange (false, std::memory_order_relaxed))
        engine_.allNotesOff();

    // Render in segments split at MIDI events so note timing is sample-accurate.
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int n = end - pos;
            // gain smoothing at block granularity of 32 samples is plenty for a bench tool
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
        // F9: the channel filter gates note-ON (and pitch bend) only -- note-off /
        // all-notes-off / all-sound-off always pass so a filtered channel can't strand a voice.
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
    delete engine_.takeRetiredBank();
    delete engine_.takeRetiredWav();
}

// ------------------------------------------------------------------ Slot W
juce::String ScoutProcessor::loadWav (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! file.existsAsFile())
        return "file not found: " + file.getFullPathName();
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return "could not read " + file.getFileName();
    std::string err;
    auto wav = WavSample::load (bytes.getData(), bytes.getSize(), file.getFileName().toStdString(), err);
    if (wav == nullptr)
        return file.getFileName() + ": " + juce::String (err);

    // the file's own metadata seeds the editor state; focus/split/fades/prefix persist across loads
    wavState_.loopStart = wav->loopStart;
    wavState_.loopEnd   = wav->loopEnd;
    wavState_.loopMode  = wav->hasSmpl ? (wav->loopType == 1 ? 1 : 0) : 0;
    wavState_.rootKey   = wav->rootKey;
    wavState_.fineCents = wav->fineCents;
    wavState_.description = juce::String::fromUTF8 (wav->bextDescription.c_str());
    if (wavState_.prefix.isEmpty())
    {
        // "<PREFIX>_<NOTE>" -> remember the prefix for SAVE AS
        const juce::String stem = file.getFileNameWithoutExtension();
        const int us = stem.lastIndexOfChar ('_');
        wavState_.prefix = (us > 0 && rootFromFileName (file.getFileName().toStdString()) >= 0) ? stem.substring (0, us) : stem;
    }
    uiWav_ = wav.get();
    wavFilePath_ = file.getFullPathName();
    wavDirty_ = false;
    delete engine_.takeRetiredWav();
    pushWavState();
    engine_.setWav (wav.release());
    wavGeneration_.fetch_add (1, std::memory_order_acq_rel);
    normaliseFocus();
    return {};
}

void ScoutProcessor::setWavState (const WavEditState& s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    WavEditState n = s;
    const uint32_t last = uiWav_ != nullptr && uiWav_->frames > 0 ? uiWav_->frames - 1 : 0;
    n.loopStart = juce::jmin (n.loopStart, last);
    n.loopEnd   = juce::jlimit (n.loopStart, last, n.loopEnd);
    n.loopMode  = juce::jlimit (0, 2, n.loopMode);
    n.rootKey   = juce::jlimit (0, 127, n.rootKey);
    if (! std::isfinite (n.fineCents)) n.fineCents = 0.0;
    n.fineCents = juce::jlimit (-50.0, 49.99, n.fineCents);
    n.attackMs  = juce::jlimit (0.0, 500.0, n.attackMs);
    n.releaseMs = juce::jlimit (10.0, 5000.0, n.releaseMs);
    n.focus     = juce::jlimit (0, 2, n.focus);
    n.splitNote = juce::jlimit (0, 127, n.splitNote);
    if (n.loopStart != wavState_.loopStart || n.loopEnd != wavState_.loopEnd || n.loopMode != wavState_.loopMode
        || n.rootKey != wavState_.rootKey || n.fineCents != wavState_.fineCents || n.description != wavState_.description)
        wavDirty_ = uiWav_ != nullptr;
    wavState_ = n;
    pushWavState();
}

void ScoutProcessor::pushWavState()
{
    engine_.setWavLoop (wavState_.loopStart, wavState_.loopEnd, (WavLoopMode) wavState_.loopMode);
    engine_.setWavTuning (wavState_.rootKey, wavState_.fineCents);
    engine_.setWavFades (wavState_.attackMs, wavState_.releaseMs);
    engine_.setFocus ((Focus) wavState_.focus, wavState_.splitNote);
}

juce::String ScoutProcessor::saveWav (const juce::File& target)
{
    JUCE_ASSERT_MESSAGE_THREAD
    // The ONLY write path in the project, and it can only serialise a WavSample
    // (the user's own file) -- SF2 pool data has no route here (CLAUDE.md §2.1).
    if (uiWav_ == nullptr) return "no WAV loaded";
    if (target.getFullPathName().isEmpty()) return "no target";
    if (! target.hasFileExtension ("wav")) return "target must be a .wav";
    WavSaveSpec spec;
    spec.loopStart = wavState_.loopStart;
    spec.loopEnd   = wavState_.loopEnd;
    spec.loopType  = wavState_.loopMode == 1 ? 1 : 0;
    spec.rootKey   = wavState_.rootKey;
    spec.fineCents = wavState_.fineCents;
    spec.export16BitMono = wavState_.export16BitMono;
    juce::String desc = wavState_.description.trim();
    const juce::String stamp = "SF2 Scout v" SF2SCOUT_VERSION_STRING " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d")
        + " loop=" + juce::String ((int) spec.loopStart) + ".." + juce::String ((int) spec.loopEnd)
        + (spec.loopType == 1 ? " pingpong" : " fwd")
        + " root=" + juce::String (spec.rootKey) + (std::fabs (spec.fineCents) > 0.005 ? juce::String::formatted ("%+.1fc", spec.fineCents) : juce::String())
        + " att=" + juce::String ((int) wavState_.attackMs) + "ms rel=" + juce::String ((int) wavState_.releaseMs) + "ms";
    desc = desc.isEmpty() ? stamp : desc + " | " + stamp;
    spec.description = desc.toStdString().substr (0, 255);
    const std::vector<uint8_t> bytes = writeWav (*uiWav_, spec);
    juce::TemporaryFile tmp (target);
    {
        juce::FileOutputStream out (tmp.getFile());
        if (! out.openedOk() || ! out.write (bytes.data(), bytes.size())) return "could not write " + target.getFileName();
        out.flush();
    }
    if (! tmp.overwriteTargetFileWithTemporary()) return "could not replace " + target.getFileName();
    wavDirty_ = false;
    // a SAVE AS (not an export) becomes the working file for the next Ctrl+S
    if (! spec.export16BitMono) wavFilePath_ = target.getFullPathName();
    return {};
}

juce::File ScoutProcessor::wavSaveAsSuggestion() const
{
    const juce::File src (wavFilePath_);
    const juce::String prefix = wavState_.prefix.isNotEmpty() ? wavState_.prefix : juce::String ("SAMPLE");
    const juce::String name = prefix + "_" + juce::String::fromUTF8 (noteName (wavState_.rootKey).c_str()) + ".wav";
    return (src.getFullPathName().isNotEmpty() ? src.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)).getChildFile (name);
}

void ScoutProcessor::readWavStateFrom (const juce::ValueTree& state)
{
    WavEditState s = wavState_;
    s.loopStart = (uint32_t) juce::jmax (0, (int) state.getProperty ("wavLoopStart", (int) s.loopStart));
    s.loopEnd   = (uint32_t) juce::jmax (0, (int) state.getProperty ("wavLoopEnd", (int) s.loopEnd));
    s.loopMode  = (int) state.getProperty ("wavLoopMode", s.loopMode);
    s.rootKey   = (int) state.getProperty ("wavRoot", s.rootKey);
    s.fineCents = (double) state.getProperty ("wavCents", s.fineCents);
    s.attackMs  = (double) state.getProperty ("wavAttackMs", s.attackMs);
    s.releaseMs = (double) state.getProperty ("wavReleaseMs", s.releaseMs);
    s.focus     = (int) state.getProperty ("focus", s.focus);
    s.splitNote = (int) state.getProperty ("splitNote", s.splitNote);
    s.export16BitMono = (bool) state.getProperty ("wavExport16Mono", s.export16BitMono);
    s.description = state.getProperty ("wavDescription", s.description).toString();
    s.prefix      = state.getProperty ("wavPrefix", s.prefix).toString();
    midiInputDevice_ = state.getProperty ("midiInputDevice", midiInputDevice_).toString();
    setWavState (s);
    wavDirty_ = false;
}

juce::String ScoutProcessor::loadSoundFont (const juce::File& file)
{
    // F2: loadSoundFont writes loadedFileName_/loadedFilePath_ and swaps the
    // bank the editor reads -- both are message-thread-only from here on.
    JUCE_ASSERT_MESSAGE_THREAD
    if (! file.existsAsFile())
        return "file not found: " + file.getFullPathName();
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return "could not read " + file.getFileName();
    std::string err;
    auto bank = SoundFontBank::load (bytes.getData(), bytes.getSize(), err);
    if (bank == nullptr)
        return file.getFileName() + ": " + juce::String (err);
    bank->setFileName (file.getFileName().toStdString());
    uiBank_ = bank.get();
    loadedFileName_ = file.getFileName();
    loadedFilePath_ = file.getFullPathName();
    presetIndex_.store (0);
    // F4: drain any retiree left by a stalled message loop before handing the
    // engine a new bank, else the engine refuses the swap (pending slot busy).
    delete engine_.takeRetiredBank();
    engine_.setBank (bank.release());
    bankGeneration_.fetch_add (1, std::memory_order_acq_rel);
    normaliseFocus();
    return {};
}

void ScoutProcessor::unloadSoundFont()
{
    JUCE_ASSERT_MESSAGE_THREAD
    uiBank_ = nullptr;
    loadedFileName_.clear();
    loadedFilePath_.clear();
    presetIndex_.store (0);
    delete engine_.takeRetiredBank();
    engine_.clearBank();                  // the audio thread kills the R voices and parks the bank for the collector
    bankGeneration_.fetch_add (1, std::memory_order_acq_rel);
    normaliseFocus();
}

void ScoutProcessor::unloadWav()
{
    JUCE_ASSERT_MESSAGE_THREAD
    uiWav_ = nullptr;
    wavFilePath_.clear();
    wavDirty_ = false;
    delete engine_.takeRetiredWav();
    engine_.clearWav();
    wavGeneration_.fetch_add (1, std::memory_order_acq_rel);
    normaliseFocus();
}

void ScoutProcessor::normaliseFocus()
{
    // The engine already falls back at note time; this keeps the CONTROL honest
    // so the operator never sees WAV highlighted while the SF2 is what sounds.
    int f = wavState_.focus;
    if (f != 0 && ! hasWav() && hasSf2()) f = 0;
    else if (f != 1 && ! hasSf2() && hasWav()) f = 1;
    if (f != wavState_.focus) { wavState_.focus = f; pushWavState(); }
}

void ScoutProcessor::setPresetIndex (int index)
{
    const int count = uiBank_ != nullptr ? uiBank_->presetCount() : 0;
    if (count <= 0) { presetIndex_.store (0); return; }
    presetIndex_.store (juce::jlimit (0, count - 1, index));
}

void ScoutProcessor::auditionNote (int note, int velocity)
{
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, velocity, true, false }; uiNoteFifo_.finishedWrite (1); }
}

void ScoutProcessor::auditionWav (int note, bool on, int velocity)
{
    if (! on) { auditionRelease (note); return; }
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, velocity, true, true }; uiNoteFifo_.finishedWrite (1); }
}

void ScoutProcessor::auditionRelease (int note)
{
    // F10: a dropped note-off would leave an eternally looping voice, so this
    // must never silently drop -- if the FIFO is full, panic-flag instead.
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, 0, false, false }; uiNoteFifo_.finishedWrite (1); }
    else uiPanic_.store (true, std::memory_order_relaxed);
}

void ScoutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty ("sf2Path", loadedFilePath_, nullptr);
    state.setProperty ("presetIndex", presetIndex_.load(), nullptr);
    // Slot W: non-param state (append-only tree properties, no APVTS params added)
    state.setProperty ("wavPath", wavFilePath_, nullptr);
    state.setProperty ("wavLoopStart", (int) wavState_.loopStart, nullptr);
    state.setProperty ("wavLoopEnd", (int) wavState_.loopEnd, nullptr);
    state.setProperty ("wavLoopMode", wavState_.loopMode, nullptr);
    state.setProperty ("wavRoot", wavState_.rootKey, nullptr);
    state.setProperty ("wavCents", wavState_.fineCents, nullptr);
    state.setProperty ("wavAttackMs", wavState_.attackMs, nullptr);
    state.setProperty ("wavReleaseMs", wavState_.releaseMs, nullptr);
    state.setProperty ("focus", wavState_.focus, nullptr);
    state.setProperty ("splitNote", wavState_.splitNote, nullptr);
    state.setProperty ("wavExport16Mono", wavState_.export16BitMono, nullptr);
    state.setProperty ("wavDescription", wavState_.description, nullptr);
    state.setProperty ("wavPrefix", wavState_.prefix, nullptr);
    state.setProperty ("midiInputDevice", midiInputDevice_, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ScoutProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // F2: hosts may call this from a load/save thread that is not the message
    // thread, racing the editor's reads and ScoutEngine::setBank's delete of
    // the previous bank. Stash the request and always run the actual load on
    // the message thread.
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts_.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts_.replaceState (state);
    const juce::String path = state.getProperty ("sf2Path", "").toString();
    pendingRestorePath_ = path;
    pendingRestorePreset_ = (int) state.getProperty ("presetIndex", 0);
    pendingRestoreWavPath_ = state.getProperty ("wavPath", "").toString();
    pendingRestoreTree_ = state.createCopy();
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
    const juce::String path = pendingRestorePath_;
    const int preset = pendingRestorePreset_;
    // Reload is best-effort: a missing file leaves the tool empty, not broken.
    if (path.isNotEmpty() && loadSoundFont (juce::File (path)).isEmpty())
        setPresetIndex (preset);
    // Slot W: the file first (its smpl seeds the state), then the saved edit on top
    if (pendingRestoreWavPath_.isNotEmpty())
        (void) loadWav (juce::File (pendingRestoreWavPath_));
    if (pendingRestoreTree_.isValid())
        readWavStateFrom (pendingRestoreTree_);
    pendingRestoreTree_ = {};
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
