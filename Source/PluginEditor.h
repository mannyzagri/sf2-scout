#pragma once

#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/Readout.h"
#include "ui/WaveEditor.h"

namespace sf2scout
{

// Fixed-size plain-JUCE face (GUI type: fixed -- CLAUDE.md §0). Layout and
// tokens follow docs/handoff-gui-v1/README.md; this file only WIRES them.
// The Slot W band (docs/SF2SCOUT_WAV_EXTENSION.md) is a function-first
// addition below the zone map: waveform editor, seam view, sample fields.
class ScoutEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer,
                    private juce::ListBoxModel
{
public:
    explicit ScoutEditor (ScoutProcessor&);
    ~ScoutEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // drag & drop anywhere on the window: .sf2 -> Slot R, .wav -> Slot W
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int kWidth = 920;
    static constexpr int kHeaderH = 48, kPresetColW = 300, kFooterH = 44;
    static constexpr int kBodyH = 310;      // preset header 42 + scroll 268 (handoff §2a)
    static constexpr int kZoneBandH = 12 + 12 + 6 + 52 + 4 + 28 + 6;
    // Slot W band rows: pad 10, label 12, gap 6, controls 26, gap 6, wave 108, gap 6, seam/fields 74, gap 4, status 14, pad 8
    static constexpr int kWaveH = 108, kLowerH = 74, kSeamW = 300;
    static constexpr int kWavBandH = 10 + 12 + 6 + 26 + 6 + kWaveH + 6 + kLowerH + 4 + 14 + 8;

private:
    // ListBoxModel (preset list)
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int row) override;

    void timerCallback() override;
    void chooseFile();
    void loadFile (const juce::File& f);
    void stepPreset (int delta);
    void rebuildForBank();
    void refreshReadout (bool force);
    void showError (const juce::String& msg);

    // Slot W
    void chooseWav();
    void loadWavFile (const juce::File& f);
    void rebuildForWav();
    void syncWavControls();
    template <typename Fn> void editWav (Fn&& fn)
    {
        WavEditState s = proc_.wavState();
        fn (s);
        proc_.setWavState (s);
        syncWavControls();
    }
    void nudge (bool endMarker, juce::int64 delta);
    void doSave();
    void doSaveAs();
    void refreshMidiDevices (bool force);      // standalone only: enumerate juce::MidiInput devices
    void applyMidiDevice();                    // standalone only: enable the chosen device in the AudioDeviceManager
    bool isStandalone() const { return proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }
    void setWavStatus (const juce::String& msg, bool isError);
    juce::Rectangle<int> wavBandBounds() const;

    ScoutProcessor& proc_;

    // header
    ui::FlatButton loadButton_ { "LOAD", ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    juce::String fileLabel_, errorText_;
    ui::SegmentSwitch modeSwitch_ { { "AS-AUTHORED", "LOOP-ONLY" } };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment_;
    ui::SegmentSwitch focusSwitch_ { { "SF2", "WAV", "SPLIT" }, 12.0f, 16 };   // "KEYBOARD PLAYS:"
    static constexpr int kKbLabelW = 104;
    void updateFocusEnables();
    ui::FlatButton unloadSf2Button_ { "UNLOAD", ui::col::inset, ui::col::text2, ui::col::hoverBtn, 3.0f, ui::mono (10.0f, true), true };
    ui::FlatButton unloadWavButton_ { "UNLOAD", ui::col::inset, ui::col::text2, ui::col::hoverBtn, 4.0f, ui::mono (10.0f, true), true };
    void unloadSf2();
    void unloadWav (bool askIfDirty);
    ui::NumField splitField_;

    // body
    ui::FlatButton prevButton_ { juce::String::fromUTF8 ("\xE2\x80\xB9"), ui::col::inset, ui::col::text, ui::col::hoverBtn, 3.0f, ui::mono (11.0f, true), true };
    ui::FlatButton nextButton_ { juce::String::fromUTF8 ("\xE2\x80\xBA"), ui::col::inset, ui::col::text, ui::col::hoverBtn, 3.0f, ui::mono (11.0f, true), true };
    juce::ListBox presetList_;
    ui::InfoReadout readout_;
    // F3: row text copied out of the bank in rebuildForBank() (message thread) so
    // paintListBoxItem never dereferences proc_.bank().
    juce::StringArray presetRowIds_, presetRowNames_;

    // zone band
    ui::ZoneMapStrip zoneStrip_;
    ui::ZoneLabelsRow zoneLabels_;

    // Slot W band
    ui::FlatButton loadWavButton_ { "LOAD WAV", ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    ui::FlatButton saveButton_    { "SAVE",     ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    ui::FlatButton saveAsButton_  { "SAVE AS",  ui::col::inset,  ui::col::text,       ui::col::hoverBtn,   4.0f, ui::buttonFont(), true };
    ui::SegmentSwitch loopModeSwitch_ { { "FWD", "PING-PONG", "OFF" } };
    ui::FlatButton playButton_ { "PLAY C3", ui::col::inset, ui::col::text, ui::col::hoverBtn, 4.0f, ui::buttonFont(), true };
    static constexpr int kPlayNote = 48;
    juce::ToggleButton snapToggle_ { "ZERO-X SNAP" };
    juce::ToggleButton exportToggle_ { "16-BIT MONO" };
    ui::WaveformView waveform_;
    ui::SeamView seam_;
    ui::NumField startField_, endField_, lenField_, rootField_, fineField_ { false }, attackField_, releaseField_;
    juce::TextEditor prefixEditor_, descEditor_;
    juce::String wavLabel_, wavStatus_;
    bool wavStatusIsError_ = false;
    juce::int64 cursorFrame_ = -1;
    ui::DisplayBuffer display_;
    int seenWavGeneration_ = -1;
    double shownWavPlayhead_ = -2.0;
    int shownWavNote_ = -2;

    // footer
    ui::MasterSliderLook sliderLook_;
    ui::ChipComboLook chipLook_;
    juce::Slider masterSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment_;
    juce::ComboBox midiCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiAttachment_;
    juce::ComboBox midiDeviceCombo_;           // standalone only
    juce::StringArray midiDeviceIds_;          // identifier per combo row (row 0 = all devices)
    int midiPollTicks_ = 0;
    juce::TooltipWindow tooltips_ { this, 400 };

    std::unique_ptr<juce::FileChooser> chooser_;
    int seenBankGeneration_ = -1;
    uint32_t seenNoteSeq_ = 0;
    int shownNote_ = -1;
    int shownPreset_ = -1;
    int shownVoices_ = -1;
    double shownPlayhead_ = -2.0;
    int shownMode_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoutEditor)
};

} // namespace sf2scout
