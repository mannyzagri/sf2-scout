#include "PluginEditor.h"

namespace sf2scout
{
using namespace ui;

ScoutEditor::ScoutEditor (ScoutProcessor& p)
    : AudioProcessorEditor (p), proc_ (p)
{
    setSize (kWidth, kHeaderH + kBodyH + kZoneBandH + kFooterH);
    setResizable (false, false);

    // header
    addAndMakeVisible (loadButton_);
    loadButton_.onClick = [this] { chooseFile(); };
    addAndMakeVisible (modeSwitch_);
    if (auto* modeParam = proc_.apvts().getParameter (ParamId::mode))
    {
        modeAttachment_ = std::make_unique<juce::ParameterAttachment> (*modeParam,
            [this] (float v) { modeSwitch_.setIndex (juce::roundToInt (v), false); refreshReadout (true); },
            proc_.apvts().undoManager);
        modeAttachment_->sendInitialUpdate();
        modeSwitch_.onChange = [this] (int i) { modeAttachment_->setValueAsCompleteGesture ((float) i); };
    }

    // preset column
    addAndMakeVisible (prevButton_); prevButton_.onClick = [this] { stepPreset (-1); };
    addAndMakeVisible (nextButton_); nextButton_.onClick = [this] { stepPreset (+1); };
    presetList_.setModel (this);
    presetList_.setRowHeight (36);      // 9px pad + 13px text + 9px pad + 1px hairline ... rounded
    presetList_.setColour (juce::ListBox::backgroundColourId, col::window);
    presetList_.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    presetList_.getViewport()->setScrollBarsShown (true, false);
    addAndMakeVisible (presetList_);
    addAndMakeVisible (readout_);

    // zone band
    addAndMakeVisible (zoneStrip_);
    addAndMakeVisible (zoneLabels_);
    zoneStrip_.onKeyDown = [this] (int n) { proc_.auditionNote (n, 100); };
    zoneStrip_.onKeyUp   = [this] (int n) { proc_.auditionRelease (n); };

    // footer
    masterSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterSlider_.setLookAndFeel (&sliderLook_);
    addAndMakeVisible (masterSlider_);
    masterAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc_.apvts(), ParamId::masterGain, masterSlider_);
    masterSlider_.onValueChange = [this] { repaint (getLocalBounds().removeFromBottom (kFooterH)); };

    midiCombo_.setLookAndFeel (&chipLook_);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (proc_.apvts().getParameter (ParamId::midiChannel)))
        midiCombo_.addItemList (ch->choices, 1);
    addAndMakeVisible (midiCombo_);
    midiAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc_.apvts(), ParamId::midiChannel, midiCombo_);

    rebuildForBank();
    startTimerHz (30);
}

ScoutEditor::~ScoutEditor()
{
    stopTimer();
    masterSlider_.setLookAndFeel (nullptr);
    midiCombo_.setLookAndFeel (nullptr);
}

// ------------------------------------------------------------------ layout
void ScoutEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderH).reduced (16, 0);
    loadButton_.setBounds (header.removeFromLeft (64).withSizeKeepingCentre (64, 30));
    auto right = header.removeFromRight (modeSwitch_.preferredWidth());
    modeSwitch_.setBounds (right.withSizeKeepingCentre (right.getWidth(), 30));

    auto body = r.removeFromTop (kBodyH);
    auto presetCol = body.removeFromLeft (kPresetColW);
    body.removeFromLeft (1);                                  // divider
    auto listHeader = presetCol.removeFromTop (42).reduced (12, 10);
    nextButton_.setBounds (listHeader.removeFromRight (24).withHeight (22));
    listHeader.removeFromRight (4);
    prevButton_.setBounds (listHeader.removeFromRight (24).withHeight (22));
    presetList_.setBounds (presetCol);
    readout_.setBounds (body);

    auto band = r.removeFromTop (kZoneBandH).reduced (16, 0);
    band.removeFromTop (12 + 12 + 6);                          // pad + label row + gap
    zoneStrip_.setBounds (band.removeFromTop (52));
    band.removeFromTop (4);
    zoneLabels_.setBounds (band.removeFromTop (28));

    auto footer = r.removeFromTop (kFooterH).reduced (16, 0);
    footer.removeFromLeft (52 + 16);                           // "MASTER" label + gap
    masterSlider_.setBounds (footer.removeFromLeft (200).withSizeKeepingCentre (200, 20));
    auto midi = footer.removeFromRight (110 + 16 + 100);       // voices + gap + chip
    midi.removeFromRight (110);
    midi.removeFromRight (16);
    midiCombo_.setBounds (midi.removeFromRight (76).withSizeKeepingCentre (76, 28));
}

void ScoutEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::window);
    auto r = getLocalBounds();

    // header
    auto header = r.removeFromTop (kHeaderH);
    g.setColour (col::headFoot); g.fillRect (header);
    g.setColour (col::borderSec); g.fillRect (header.removeFromBottom (1));
    auto hc = getLocalBounds().removeFromTop (kHeaderH).reduced (16, 0);
    hc.removeFromLeft (64 + 14);
    auto modeArea = hc.removeFromRight (modeSwitch_.preferredWidth() + 10 + 36);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText ("MODE", modeArea.removeFromLeft (36), juce::Justification::centredLeft, false);
    hc.removeFromRight (14);
    auto fileBlock = hc.withSizeKeepingCentre (hc.getWidth(), 30);
    g.drawText ("SOUNDFONT", fileBlock.removeFromTop (12), juce::Justification::centredLeft, false);
    fileBlock.removeFromTop (2);
    g.setFont (mono (13.0f, true));
    if (errorText_.isNotEmpty()) { g.setColour (col::warn); g.drawText (errorText_, fileBlock, juce::Justification::centredLeft, true); }
    else { g.setColour (col::text); g.drawText (fileLabel_, fileBlock, juce::Justification::centredLeft, true); }

    // body
    auto body = r.removeFromTop (kBodyH);
    auto presetCol = body.removeFromLeft (kPresetColW);
    g.setColour (col::borderSec); g.fillRect (body.removeFromLeft (1));
    auto listHeader = presetCol.removeFromTop (42);
    g.setColour (col::hairline); g.fillRect (listHeader.removeFromBottom (1));
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText ("PRESETS", listHeader.reduced (12, 0), juce::Justification::centredLeft, false);

    // zone band
    auto band = r.removeFromTop (kZoneBandH);
    g.setColour (col::zoneBand); g.fillRect (band);
    g.setColour (col::borderSec); g.fillRect (band.removeFromTop (1));
    auto bandLabels = band.reduced (16, 0).withTrimmedTop (11).withHeight (12);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText (juce::String::fromUTF8 ("ZONE MAP \xE2\x80\x94 CLICK A KEY TO AUDITION"), bandLabels, juce::Justification::centredLeft, false);
    g.drawText (juce::String (zoneStrip_.zoneCount()) + " ZONES", bandLabels, juce::Justification::centredRight, false);

    // footer
    auto footer = r.removeFromTop (kFooterH);
    g.setColour (col::headFoot); g.fillRect (footer);
    g.setColour (col::borderSec); g.fillRect (footer.removeFromTop (1));
    auto fc = getLocalBounds().removeFromBottom (kFooterH).reduced (16, 0);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText ("MASTER", fc.removeFromLeft (52), juce::Justification::centredLeft, false);
    fc.removeFromLeft (16 + 200 + 16);
    const double gain = masterSlider_.getValue();
    const double db = gain > 0.0 ? 20.0 * std::log10 (gain) : -120.0;
    g.setFont (mono (12.0f, true)); g.setColour (col::text);
    g.drawText (gain > 0.0 ? juce::String (db, 1) + " dB" : juce::String::fromUTF8 ("-\xE2\x88\x9E dB"), fc.removeFromLeft (56), juce::Justification::centredLeft, false);
    auto voices = fc.removeFromRight (110);
    const int n = proc_.engine().activeVoiceCount();
    g.setColour (n > 0 ? col::okDot : col::idleDot);
    g.fillEllipse (juce::Rectangle<float> ((float) voices.getX(), (float) voices.getCentreY() - 4.0f, 8.0f, 8.0f));
    g.setFont (mono (12.0f)); g.setColour (col::text2);
    g.drawText (juce::String (n) + "/" + juce::String (ScoutEngine::kMaxVoices) + " voices", voices.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    fc.removeFromRight (16 + 76 + 8);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText ("MIDI IN", fc.removeFromRight (44), juce::Justification::centredLeft, false);

    // build stamp: "which build is this?" must take zero round-trips
    g.setFont (mono (9.0f)); g.setColour (col::text3);
    g.drawText (juce::String ("v") + ScoutProcessor::kBuildStamp, getLocalBounds().removeFromBottom (kFooterH).reduced (16, 0).removeFromLeft (300).withTrimmedLeft (52 + 16 + 200 + 16 + 56).withTrimmedTop (2),
                juce::Justification::centredLeft, false);
}

// ------------------------------------------------------------------ list
int ScoutEditor::getNumRows()
{
    const auto* b = proc_.bank();
    return b != nullptr ? b->presetCount() : 0;
}

void ScoutEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto* b = proc_.bank();
    if (b == nullptr || row < 0 || row >= b->presetCount()) return;
    const Preset& p = b->presets()[(size_t) row];
    if (selected) { g.setColour (col::selRow); g.fillRect (0, 0, w, h); }
    g.setColour (col::hairlineRow); g.fillRect (0, h - 1, w, 1);
    auto r = juce::Rectangle<int> (12, 0, w - 24, h - 1);
    const juce::String id = InfoReadout::presetId (p);
    g.setFont (mono (11.0f));
    g.setColour (selected ? col::accent : col::text3);
    const int idW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (11.0f), id));
    g.drawText (id, r.removeFromLeft (idW), juce::Justification::centredLeft, false);
    r.removeFromLeft (10);
    g.setFont (sans (13.0f)); g.setColour (col::text);
    g.drawText (S (p.name), r, juce::Justification::centredLeft, true);
}

void ScoutEditor::listBoxItemClicked (int row, const juce::MouseEvent&) { selectedRowsChanged (row); }

void ScoutEditor::selectedRowsChanged (int row)
{
    if (row < 0) return;
    proc_.setPresetIndex (row);
    refreshReadout (true);
}

void ScoutEditor::stepPreset (int delta)
{
    const int n = getNumRows();
    if (n == 0) return;
    const int next = ((proc_.presetIndex() + delta) % n + n) % n;     // wraps at both ends
    proc_.setPresetIndex (next);
    presetList_.selectRow (next, true, true);
    refreshReadout (true);
}

// ------------------------------------------------------------------ loading
void ScoutEditor::chooseFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a SoundFont", juce::File(), "*.sf2;*.SF2");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) loadFile (f);
        });
}

bool ScoutEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".sf2")) return true;
    return false;
}

void ScoutEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".sf2")) { loadFile (juce::File (f)); return; }
}

void ScoutEditor::loadFile (const juce::File& f)
{
    const juce::String err = proc_.loadSoundFont (f);
    if (err.isNotEmpty()) { showError (err); return; }
    errorText_.clear();
    rebuildForBank();
}

void ScoutEditor::showError (const juce::String& msg)
{
    errorText_ = "ERROR: " + msg;     // shown in the filename slot; previous state kept (spec §1)
    repaint();
}

void ScoutEditor::rebuildForBank()
{
    seenBankGeneration_ = proc_.bankGeneration();
    fileLabel_ = proc_.bank() != nullptr ? proc_.loadedFileName() : juce::String::fromUTF8 ("\xE2\x80\x94 no file loaded \xE2\x80\x94");
    presetList_.updateContent();
    if (getNumRows() > 0) presetList_.selectRow (proc_.presetIndex(), true, true);
    shownNote_ = -1;
    seenNoteSeq_ = proc_.engine().lastNote().sequence;
    refreshReadout (true);
    repaint();
}

// ------------------------------------------------------------------ polling
void ScoutEditor::refreshReadout (bool force)
{
    const auto info = proc_.engine().lastNote();
    const auto* bank = proc_.bank();
    const int mode = juce::roundToInt (proc_.apvts().getRawParameterValue (ParamId::mode)->load());
    const double playhead = proc_.engine().lastPlayhead();
    const bool newNote = info.sequence != seenNoteSeq_;
    if (! force && ! newNote && shownPlayhead_ == playhead && shownMode_ == mode) return;

    ReadoutState s;
    s.bank = bank;
    s.presetIndex = proc_.presetIndex();
    s.mode = (PlayMode) mode;
    s.playhead = playhead;
    if (newNote) { shownNote_ = info.note; shownPreset_ = info.presetIndex; seenNoteSeq_ = info.sequence; }
    s.note = shownNote_;
    // The zone described is resolved on the message thread from the bank the UI holds.
    // A note played on a previous preset keeps describing that preset's zone until the next note.
    if (bank != nullptr && shownNote_ >= 0)
    {
        const int pi = (shownPreset_ >= 0 && shownPreset_ < bank->presetCount()) ? shownPreset_ : s.presetIndex;
        if (newNote && info.zoneIndex >= 0 && pi < bank->presetCount() && info.zoneIndex < (int) bank->presets()[(size_t) pi].zones.size())
            s.zone = &bank->presets()[(size_t) pi].zones[(size_t) info.zoneIndex];
        else
            s.zone = bank->zoneForKey (pi, shownNote_);
        if (newNote) shownPreset_ = pi;
    }
    shownPlayhead_ = playhead;
    shownMode_ = mode;
    readout_.update (s);
    // the strip follows the active (selected) preset, the readout the last note
    ReadoutState stripState = s;
    stripState.presetIndex = proc_.presetIndex();
    zoneStrip_.update (stripState);
    zoneLabels_.update (zoneStrip_);
    if (force || newNote) repaint (getLocalBounds().removeFromTop (kHeaderH + kBodyH + kZoneBandH).removeFromBottom (kZoneBandH).removeFromTop (30));
}

void ScoutEditor::timerCallback()
{
    if (proc_.bankGeneration() != seenBankGeneration_) rebuildForBank();
    refreshReadout (false);
    const int v = proc_.engine().activeVoiceCount();
    if (v != shownVoices_) { shownVoices_ = v; repaint (getLocalBounds().removeFromBottom (kFooterH)); }
}

} // namespace sf2scout
