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
    const int numSamples = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float scratchRight[1];
    (void) scratchRight;

    engine_.setMode ((PlayMode) (int) modeParam_->load());
    engine_.setPreset (presetIndex_.load (std::memory_order_relaxed));
    const int channelFilter = (int) channelParam_->load();   // 0 = omni

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
            float* r = right != nullptr ? right + pos : left + pos;
            if (right == nullptr)
            {
                // mono host: render L and discard R by rendering both into L (sum)
                engine_.process (left + pos, left + pos, chunk, g * 0.5f);
            }
            else
                engine_.process (left + pos, r, chunk, g);
            pos += chunk;
        }
    };
    gainSmoother_.setTargetValue (gainParam_->load());

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int at = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderTo (at);
        if (channelFilter != 0 && msg.getChannel() != channelFilter) continue;
        if (msg.isNoteOn())        engine_.noteOn (msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())  engine_.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine_.allNotesOff();
        else if (msg.isPitchWheel())
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
    engine_.setBank (bank.release());
    ++bankGeneration_;
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
    int start1, size1, start2, size2;
    uiNoteFifo_.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) { uiNotes_[(size_t) start1] = { note, 0, false }; uiNoteFifo_.finishedWrite (1); }
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
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts_.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts_.replaceState (state);
    const juce::String path = state.getProperty ("sf2Path", "").toString();
    const int preset = (int) state.getProperty ("presetIndex", 0);
    if (path.isNotEmpty())
    {
        // Reload is best-effort: a missing file leaves the tool empty, not broken.
        if (loadSoundFont (juce::File (path)).isEmpty())
            setPresetIndex (preset);
    }
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
