#pragma once

#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/Lists.h"
#include "ui/Readout.h"
#include "ui/WaveEditor.h"
#include "ui/ExportColumn.h"

namespace sf2scout
{

// The v2 face (docs/handoff-gui-v2/README.md): a 1200 x 840 canvas held in
// `face_`, scaled uniformly 0.75-1.5 by the window (fixed aspect). Every
// bound below is the manifest's; this file only WIRES the design to the
// processor. Plain JUCE components (deviation D-1).
class ScoutEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit ScoutEditor (ScoutProcessor&);
    ~ScoutEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int kW = 1200, kH = 840;

private:
    // the 1200 x 840 canvas: paints the static chrome, hosts every widget
    struct Face : public juce::Component
    {
        explicit Face (ScoutEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintFace (g); }
        ScoutEditor& ed;
    };
    void paintFace (juce::Graphics&);
    void timerCallback() override;

    // rebuilds from the processor (on every generation bump)
    void rebuildAll();
    void rebuildHeader();
    void rebuildLists();
    void rebuildReadouts();
    void rebuildZoneMap();
    void rebuildEditor();
    void rebuildExport();
    void refreshAssists();
    void refreshLive();

    // actions
    void chooseSource();
    void loadFile (const juce::File& f);
    void unloadSelected();
    void assign (int slot);
    void step (int delta);
    void setStatus (const juce::String& msg, bool warn);
    void applyEdit (const std::function<void (SampleEdits&)>& fn);
    void nudge (bool endMarker, juce::int64 delta);
    void setLoopKind (int kind);
    void doSave();
    void doSaveAs();
    void doSuggest();
    void doAutoRoot();
    void togglePlay();
    void browseFolder();
    void quickTarget (int which);
    void exportSample();
    void exportRange();
    void batchToggle();
    void zoneKey (int key, bool down);
    void refreshMidiDevices (bool force);
    void applyMidiDevice();
    bool isStandalone() const { return proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }
    SampleKey cur() const { return proc_.current(); }
    static int parseNote (const juce::String& s);          // "C4", "F#3", "60" -> MIDI note, -1 if not a note

    ScoutProcessor& proc_;
    Face face_ { *this };

    // header
    ui::FlatButton loadButton_ { "LOAD", ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    ui::SegmentSwitch kbSeg_ { { "A", "B", "SPLIT", "TOGGLE" }, { 44, 44, 62, 70 }, 12.0f };
    ui::NumField splitField_ { ui::NumField::Kind::Note };
    ui::SegmentSwitch modeSeg_ { { "AS-AUTHORED", "LOOP-ONLY" }, { 95, 82 }, 11.0f };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment_;
    juce::String sourceSummary_;

    // body
    ui::SourceListView sourceList_;
    ui::FlatButton unloadButton_ { "UNLOAD", ui::col::inset, ui::col::text2, ui::col::hoverBtn, 3.0f, ui::mono (10.0f, true), true };
    ui::ContentsListView contents_;
    ui::FlatButton assignA_ { juce::String::fromUTF8 ("\xE2\x86\x92 A"), ui::col::inset, ui::col::text2, ui::col::hoverBtn, 3.0f, ui::mono (10.0f, true), true };
    ui::FlatButton assignB_ { juce::String::fromUTF8 ("\xE2\x86\x92 B"), ui::col::inset, ui::col::text2, ui::col::hoverBtn, 3.0f, ui::mono (10.0f, true), true };
    ui::FlatButton prevButton_ { juce::String::fromUTF8 ("\xE2\x80\xB9"), ui::col::inset, ui::col::text, ui::col::hoverBtn, 3.0f, ui::mono (11.0f, true), true };
    ui::FlatButton nextButton_ { juce::String::fromUTF8 ("\xE2\x80\xBA"), ui::col::inset, ui::col::text, ui::col::hoverBtn, 3.0f, ui::mono (11.0f, true), true };
    juce::String listKind_, listTitle_;
    ui::ReadoutColumn readoutA_, readoutB_;

    // zone band
    ui::ZoneMapStrip zoneStrip_;
    ui::ZoneLabelsRow zoneLabels_;
    juce::String zoneTitle_, zoneCount_;
    int lastSf2Note_ = -1;
    int sfSlot_ = -1;                       // the slot whose SF2 zone the strip follows (-1 none)

    // editor
    ui::FlatButton playButton_ { "PLAY", ui::col::inset, ui::col::text, ui::col::hoverBtn, 4.0f, ui::buttonFont(), true };
    ui::SegmentSwitch loopSeg_ { { "FWD", "PING-PONG", "OFF" }, { 44, 83, 44 }, 11.0f };
    ui::Checkbox snapToggle_ { "ZERO-X SNAP" };
    ui::FlatButton suggestButton_  { "SUGGEST", ui::col::inset, ui::col::text, ui::col::hoverBtn, 4.0f, ui::buttonFont(), true };
    ui::FlatButton autoRootButton_ { "AUTO-DETECT ROOT", ui::col::inset, ui::col::text, ui::col::hoverBtn, 4.0f, ui::buttonFont(), true };
    ui::FlatButton saveAsButton_   { "SAVE AS", ui::col::inset, ui::col::text, ui::col::hoverBtn, 4.0f, ui::buttonFont(), true };
    ui::FlatButton saveButton_     { "SAVE", ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    ui::WaveformView waveform_;
    ui::SeamView seam_;
    ui::NumField startField_, endField_, lenField_;
    ui::ReadonlyField periods_;
    ui::NumField rootField_ { ui::NumField::Kind::Note }, fineField_, attackField_, releaseField_;
    ui::ClickMeter clickMeter_;
    ui::ReadonlyField loudness_;
    ui::NumField prefixField_ { ui::NumField::Kind::Text }, descField_ { ui::NumField::Kind::Text };
    juce::String editorBadge_, editorTitle_, status_, cursorLine_;
    juce::Colour editorBadgeBg_ = ui::col::idleDot;
    bool statusWarn_ = false;
    juce::int64 cursorFrame_ = -1;
    ui::DisplayBuffer display_;
    SampleKey displayKey_;
    bool assistsDirty_ = false;
    juce::uint32 assistsAt_ = 0;

    // export column
    ui::ExportColumn exportCol_;

    // footer
    ui::MasterSliderLook sliderLook_;
    ui::ChipComboLook chipLook_;
    juce::Slider masterSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment_;
    juce::ComboBox midiCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiAttachment_;
    juce::ComboBox midiDeviceCombo_;
    juce::StringArray midiDeviceIds_;
    int midiPollTicks_ = 0;
    int shownVoices_ = -1;
    juce::TooltipWindow tooltips_ { this, 400 };
    std::unique_ptr<juce::FileChooser> chooser_;

    // live state
    int seenGeneration_ = -1;
    uint32_t seenNoteSeq_[kSlots] { 0, 0, 0 };
    double shownPlayhead_ = -2.0;
    juce::uint32 latchedAt_ = 0;            // a one-shot that ran out unlatches PLAY, but only after the audio thread had its turn
    bool qwertyDown_[12] {};
    static constexpr int kQwertyBase = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoutEditor)
};

} // namespace sf2scout
