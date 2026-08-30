#pragma once

#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/Readout.h"

namespace sf2scout
{

// Fixed-size plain-JUCE face (GUI type: fixed -- CLAUDE.md §0). Layout and
// tokens follow docs/handoff-gui-v1/README.md; this file only WIRES them.
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

    // drag & drop of .sf2 anywhere on the window
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int kWidth = 920;
    static constexpr int kHeaderH = 48, kPresetColW = 300, kFooterH = 44;
    static constexpr int kBodyH = 310;      // preset header 42 + scroll 268 (handoff §2a)
    static constexpr int kZoneBandH = 12 + 12 + 6 + 52 + 4 + 28 + 6;

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

    ScoutProcessor& proc_;

    // header
    ui::FlatButton loadButton_ { "LOAD", ui::col::accent, juce::Colours::white, ui::col::accentHov, 4.0f, ui::buttonFont() };
    juce::String fileLabel_, errorText_;
    ui::SegmentSwitch modeSwitch_ { { "AS-AUTHORED", "LOOP-ONLY" } };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment_;

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

    // footer
    ui::MasterSliderLook sliderLook_;
    ui::ChipComboLook chipLook_;
    juce::Slider masterSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment_;
    juce::ComboBox midiCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiAttachment_;
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
