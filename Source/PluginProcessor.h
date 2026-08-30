#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "engine/ScoutEngine.h"

namespace sf2scout
{

// Parameter IDs are a public API -- append-only, never rename (CLAUDE.md §2).
namespace ParamId
{
    inline constexpr const char* masterGain  = "masterGain";   // float 0..1, default 0.72
    inline constexpr const char* mode        = "mode";         // choice: 0 AS-AUTHORED, 1 LOOP-ONLY
    inline constexpr const char* midiChannel = "midiChannel";  // choice: 0 OMNI, 1..16
}

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

    // Loads a file. Returns an empty string on success, else the error message.
    // Never throws; on failure the previous bank stays loaded (spec §1).
    juce::String loadSoundFont (const juce::File& file);
    juce::String loadedFileName() const { return loadedFileName_; }
    juce::String loadedFilePath() const { return loadedFilePath_; }

    // The bank the UI should read (may be nullptr). Valid until the next load.
    const SoundFontBank* bank() const { return uiBank_; }
    int  presetIndex() const { return presetIndex_.load(); }
    void setPresetIndex (int index);      // clamps; wraps are the UI's job

    // UI-driven audition (zone strip click) -- queued to the audio thread.
    void auditionNote (int note, int velocity = 100);
    void auditionRelease (int note);

    ScoutEngine& engine() { return engine_; }
    const ScoutEngine& engine() const { return engine_; }

    // bumps every time a new bank lands, so the editor can rebuild its lists
    int bankGeneration() const { return bankGeneration_.load (std::memory_order_acquire); }

    static constexpr const char* kBuildStamp = SF2SCOUT_VERSION_STRING " " __DATE__ " " __TIME__;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleAsyncUpdate() override;      // F2: runs the restore-triggered load on the message thread
    void performPendingRestore();           // JUCE_ASSERT_MESSAGE_THREAD inside

    juce::AudioProcessorValueTreeState apvts_;
    ScoutEngine engine_;

    std::atomic<float>* gainParam_    = nullptr;
    std::atomic<float>* modeParam_    = nullptr;
    std::atomic<float>* channelParam_ = nullptr;
    juce::SmoothedValue<float> gainSmoother_;
    int lastChannelFilter_ = -1;    // audio thread only (F9): detects midiChannel changes block-to-block

    std::atomic<int> presetIndex_ { 0 };
    const SoundFontBank* uiBank_ = nullptr;      // message-thread alias of the requested bank
    juce::String loadedFileName_, loadedFilePath_;   // message-thread only, writes assert JUCE_ASSERT_MESSAGE_THREAD
    std::atomic<int> bankGeneration_ { 0 };

    // F2: setStateInformation may run on any host thread; the actual load is
    // deferred to the message thread (AsyncUpdater cancels any pending update
    // in its destructor, so a destroyed processor is never touched).
    juce::String pendingRestorePath_;
    int pendingRestorePreset_ = 0;

    // UI -> audio thread note queue (lock-free, single producer / single consumer)
    struct UiNote { int note; int velocity; bool on; };
    juce::AbstractFifo uiNoteFifo_ { 64 };
    std::array<UiNote, 64> uiNotes_ {};
    std::atomic<bool> uiPanic_ { false };   // F10: set when auditionRelease can't queue a note-off

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoutProcessor)
};

} // namespace sf2scout
