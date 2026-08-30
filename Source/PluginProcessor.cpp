#include "PluginProcessor.h"
#include "PluginEditor.h"

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
            if (n.on) engine_.noteOn (n.note, n.velocity); else engine_.noteOff (n.note);
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
    return {};
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
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, velocity, true }; uiNoteFifo_.finishedWrite (1); }
}

void ScoutProcessor::auditionRelease (int note)
{
    // F10: a dropped note-off would leave an eternally looping voice, so this
    // must never silently drop -- if the FIFO is full, panic-flag instead.
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, 0, false }; uiNoteFifo_.finishedWrite (1); }
    else uiPanic_.store (true, std::memory_order_relaxed);
}

void ScoutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty ("sf2Path", loadedFilePath_, nullptr);
    state.setProperty ("presetIndex", presetIndex_.load(), nullptr);
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
    if (path.isEmpty()) return;

    pendingRestorePath_ = path;
    pendingRestorePreset_ = (int) state.getProperty ("presetIndex", 0);
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
    if (path.isEmpty()) return;
    // Reload is best-effort: a missing file leaves the tool empty, not broken.
    if (loadSoundFont (juce::File (path)).isEmpty())
        setPresetIndex (preset);
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
