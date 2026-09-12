#include "PluginEditor.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace sf2scout
{
using namespace ui;

ScoutEditor::ScoutEditor (ScoutProcessor& p)
    : AudioProcessorEditor (p), proc_ (p)
{
    setSize (kWidth, kHeaderH + kBodyH + kZoneBandH + kWavBandH + kFooterH);
    setResizable (false, false);
    setWantsKeyboardFocus (true);      // F/P/O, marker nudge keys, Ctrl+S

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

    // header: focus switch R / W / SPLIT + split point (non-param state)
    addAndMakeVisible (focusSwitch_);
    focusSwitch_.setTooltip ("Which slot the MIDI keyboard plays: the SF2 preset, the WAV, or SPLIT at the note in the box (below -> SF2, at/above -> WAV). An empty slot is greyed and falls back to the loaded one.");
    focusSwitch_.onChange = [this] (int i) { editWav ([i] (WavEditState& w) { w.focus = i; }); };
    addAndMakeVisible (unloadSf2Button_);
    unloadSf2Button_.setTooltip ("Unload the SoundFont: its voices stop, list and readout clear");
    unloadSf2Button_.onClick = [this] { unloadSf2(); };
    addAndMakeVisible (toWavButton_);
    toWavButton_.setTooltip ("Send the zone shown in the readout to Slot W (decoded copy: edit loops, export as WAV). The SF2 is never written.");
    toWavButton_.onClick = [this] { sendZoneToWav(); };
    addAndMakeVisible (unloadWavButton_);
    unloadWavButton_.setTooltip ("Unload the WAV: its voices stop, waveform clears (asks first if markers are unsaved)");
    unloadWavButton_.onClick = [this] { unloadWav (true); };
    addAndMakeVisible (splitField_);
    splitField_.setTooltip ("SPLIT point (MIDI note): below -> R, at/above -> W");
    splitField_.onCommit = [this] (double v) { editWav ([v] (WavEditState& w) { w.splitNote = (int) std::llround (v); }); };

    // Slot W band
    addAndMakeVisible (loadWavButton_); loadWavButton_.onClick = [this] { chooseWav(); };
    addAndMakeVisible (saveButton_);    saveButton_.onClick    = [this] { doSave(); };
    addAndMakeVisible (saveAsButton_);  saveAsButton_.onClick  = [this] { doSaveAs(); };
    addAndMakeVisible (loopModeSwitch_);
    loopModeSwitch_.onChange = [this] (int i) { editWav ([i] (WavEditState& w) { w.loopMode = i; }); };
    for (auto* t : { &snapToggle_, &exportToggle_ })
    {
        t->setColour (juce::ToggleButton::textColourId, col::text2);
        t->setColour (juce::ToggleButton::tickColourId, col::accent);
        t->setColour (juce::ToggleButton::tickDisabledColourId, col::control);
        addAndMakeVisible (*t);
    }
    snapToggle_.setToggleState (true, juce::dontSendNotification);
    snapToggle_.onClick = [this] { waveform_.setSnap (snapToggle_.getToggleState()); };
    // PLAY: latching C3 on the WAV regardless of focus; note-on/off travel the same FIFO as MIDI
    playButton_.setClickingTogglesState (true);
    playButton_.setTooltip ("Latch a C3 (MIDI 48) on the WAV, independent of MIDI and focus; unlatch = note-off (release fade)");
    playButton_.onClick = [this] { proc_.auditionWav (kPlayNote, playButton_.getToggleState()); };
    addAndMakeVisible (playButton_);
    exportToggle_.setTooltip ("SAVE AS re-encodes as 16-bit PCM mono (-3 dB fold) at the source rate; SAVE never degrades the source file");
    exportToggle_.onClick = [this] { editWav ([this] (WavEditState& w) { w.export16BitMono = exportToggle_.getToggleState(); }); };
    addAndMakeVisible (waveform_);
    addAndMakeVisible (seam_);
    waveform_.onWantsFocus = [this] { grabKeyboardFocus(); };
    waveform_.onCursor = [this] (juce::int64 f) { cursorFrame_ = f; repaint (wavBandBounds().removeFromBottom (8 + 14)); };
    waveform_.onLoopDragged = [this] (juce::int64 a, juce::int64 b)
    {
        editWav ([a, b] (WavEditState& w) { w.loopStart = (uint32_t) juce::jmax<juce::int64> (0, a); w.loopEnd = (uint32_t) juce::jmax<juce::int64> (0, b); });
    };
    struct FieldDef { ui::NumField* f; const char* tip; };
    for (auto d : { FieldDef { &startField_, "LOOP START (samples)" }, FieldDef { &endField_, "LOOP END (samples, last sample INCLUDED)" },
                    FieldDef { &lenField_, "LOOP LENGTH (samples) -> moves LOOP END" }, FieldDef { &rootField_, "ROOT KEY (MIDI note, C4 = 60)" },
                    FieldDef { &fineField_, "FINE TUNE (cents, -50..+50)" }, FieldDef { &attackField_, "ATTACK fade-in (ms)" }, FieldDef { &releaseField_, "RELEASE fade (ms); the loop keeps cycling under it" } })
    {
        d.f->setTooltip (d.tip);
        addAndMakeVisible (*d.f);
    }
    startField_.onCommit   = [this] (double v) { editWav ([v] (WavEditState& w) { w.loopStart = (uint32_t) juce::jmax (0.0, v); if (w.loopEnd < w.loopStart) w.loopEnd = w.loopStart; }); };
    endField_.onCommit     = [this] (double v) { editWav ([v] (WavEditState& w) { w.loopEnd = (uint32_t) juce::jmax (0.0, v); if (w.loopStart > w.loopEnd) w.loopStart = w.loopEnd; }); };
    lenField_.onCommit     = [this] (double v) { editWav ([v] (WavEditState& w) { w.loopEnd = (uint32_t) ((double) w.loopStart + juce::jmax (1.0, v) - 1.0); }); };
    rootField_.onCommit    = [this] (double v) { editWav ([v] (WavEditState& w) { w.rootKey = (int) std::llround (v); }); };
    fineField_.onCommit    = [this] (double v) { editWav ([v] (WavEditState& w) { w.fineCents = v; }); };
    attackField_.onCommit  = [this] (double v) { editWav ([v] (WavEditState& w) { w.attackMs = v; }); };
    releaseField_.onCommit = [this] (double v) { editWav ([v] (WavEditState& w) { w.releaseMs = v; }); };
    for (auto* t : { &prefixEditor_, &descEditor_ })
    {
        t->setFont (mono (12.0f));
        t->setIndents (4, 3);
        t->setColour (juce::TextEditor::backgroundColourId, col::inset);
        t->setColour (juce::TextEditor::outlineColourId, col::control);
        t->setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
        t->setColour (juce::TextEditor::textColourId, col::text);
        addAndMakeVisible (*t);
    }
    prefixEditor_.setTextToShowWhenEmpty ("PREFIX", col::text3);
    descEditor_.setTextToShowWhenEmpty ("bext description (provenance)", col::text3);
    prefixEditor_.onTextChange = [this] { editWav ([this] (WavEditState& w) { w.prefix = prefixEditor_.getText().trim(); }); };
    descEditor_.onTextChange   = [this] { editWav ([this] (WavEditState& w) { w.description = descEditor_.getText(); }); };

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
    // MIDI input DEVICE: standalone only (a host feeds the VST3 its MIDI)
    midiDeviceCombo_.setLookAndFeel (&chipLook_);
    midiDeviceCombo_.setTooltip ("MIDI input device for the standalone (the plugin gets MIDI from the host)");
    midiDeviceCombo_.onChange = [this]
    {
        const int row = midiDeviceCombo_.getSelectedItemIndex();
        proc_.setMidiInputDevice (row > 0 && row < midiDeviceIds_.size() ? midiDeviceIds_[row] : juce::String());
        applyMidiDevice();
    };
    addChildComponent (midiDeviceCombo_);
    midiDeviceCombo_.setVisible (isStandalone());
    if (isStandalone()) { refreshMidiDevices (true); applyMidiDevice(); }

    rebuildForBank();
    rebuildForWav();
    startTimerHz (30);
}

ScoutEditor::~ScoutEditor()
{
    stopTimer();
    masterSlider_.setLookAndFeel (nullptr);
    midiCombo_.setLookAndFeel (nullptr);
    midiDeviceCombo_.setLookAndFeel (nullptr);
    if (playButton_.getToggleState()) proc_.auditionWav (kPlayNote, false);
}

// ------------------------------------------------------------------ layout
void ScoutEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderH).reduced (16, 0);
    loadButton_.setBounds (header.removeFromLeft (64).withSizeKeepingCentre (64, 30));
    auto right = header.removeFromRight (modeSwitch_.preferredWidth());
    modeSwitch_.setBounds (right.withSizeKeepingCentre (right.getWidth(), 30));
    header.removeFromRight (36 + 10 + 18);                    // "MODE" label + gaps
    splitField_.setBounds (header.removeFromRight (44).withSizeKeepingCentre (44, 24));
    header.removeFromRight (6);
    auto focus = header.removeFromRight (focusSwitch_.preferredWidth());
    focusSwitch_.setBounds (focus.withSizeKeepingCentre (focus.getWidth(), 32));

    auto body = r.removeFromTop (kBodyH);
    auto presetCol = body.removeFromLeft (kPresetColW);
    body.removeFromLeft (1);                                  // divider
    auto listHeader = presetCol.removeFromTop (42).reduced (12, 10);
    nextButton_.setBounds (listHeader.removeFromRight (24).withHeight (22));
    listHeader.removeFromRight (4);
    prevButton_.setBounds (listHeader.removeFromRight (24).withHeight (22));
    listHeader.removeFromRight (10);
    unloadSf2Button_.setBounds (listHeader.removeFromRight (58).withHeight (22));
    listHeader.removeFromRight (6);
    toWavButton_.setBounds (listHeader.removeFromRight (40).withHeight (22));
    presetList_.setBounds (presetCol);
    readout_.setBounds (body);

    auto band = r.removeFromTop (kZoneBandH).reduced (16, 0);
    band.removeFromTop (12 + 12 + 6);                          // pad + label row + gap
    zoneStrip_.setBounds (band.removeFromTop (52));
    band.removeFromTop (4);
    zoneLabels_.setBounds (band.removeFromTop (28));

    // Slot W band
    auto wb = r.removeFromTop (kWavBandH).reduced (16, 0);
    wb.removeFromTop (10 + 12 + 6);
    auto ctl = wb.removeFromTop (26);
    loadWavButton_.setBounds (ctl.removeFromLeft (80));
    ctl.removeFromLeft (6);
    unloadWavButton_.setBounds (ctl.removeFromLeft (62));
    ctl.removeFromLeft (14 + 30);                              // gap + "LOOP" label
    loopModeSwitch_.setBounds (ctl.removeFromLeft (loopModeSwitch_.preferredWidth()));
    ctl.removeFromLeft (14);
    snapToggle_.setBounds (ctl.removeFromLeft (110));
    ctl.removeFromLeft (10);
    playButton_.setBounds (ctl.removeFromLeft (80));
    saveButton_.setBounds (ctl.removeFromRight (60));
    ctl.removeFromRight (6);
    saveAsButton_.setBounds (ctl.removeFromRight (76));
    ctl.removeFromRight (12);
    exportToggle_.setBounds (ctl.removeFromRight (110));
    wb.removeFromTop (6);
    waveform_.setBounds (wb.removeFromTop (kWaveH));
    wb.removeFromTop (6);
    auto lower = wb.removeFromTop (kLowerH);
    seam_.setBounds (lower.removeFromLeft (kSeamW));
    lower.removeFromLeft (12);
    auto rowA = lower.removeFromTop (33).withTrimmedTop (11);
    lower.removeFromTop (8);
    auto rowB = lower.removeFromTop (33).withTrimmedTop (11);
    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (8); };
    place (rowA, startField_, 96); place (rowA, endField_, 96); place (rowA, lenField_, 96); place (rowA, rootField_, 60); place (rowA, fineField_, 60);
    place (rowB, attackField_, 60); place (rowB, releaseField_, 60); place (rowB, prefixEditor_, 130);
    descEditor_.setBounds (rowB);

    auto footer = r.removeFromTop (kFooterH).reduced (16, 0);
    footer.removeFromLeft (52 + 16);                           // "MASTER" label + gap
    masterSlider_.setBounds (footer.removeFromLeft (200).withSizeKeepingCentre (200, 20));
    auto midi = footer.removeFromRight (110 + 16 + 100);       // voices + gap + chip
    midi.removeFromRight (110);
    midi.removeFromRight (16);
    midiCombo_.setBounds (midi.removeFromRight (76).withSizeKeepingCentre (76, 28));
    footer.removeFromRight (8 + 44 + 6);                       // gap + "MIDI IN" label + gap
    midiDeviceCombo_.setBounds (footer.removeFromRight (150).withSizeKeepingCentre (150, 28));
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
    hc.removeFromRight (18 + 44 + 6 + focusSwitch_.preferredWidth());
    auto focusLabel = hc.removeFromRight (kKbLabelW);
    g.setFont (smallLabel()); g.setColour (col::text2);
    g.drawText ("KEYBOARD PLAYS:", focusLabel, juce::Justification::centredRight, false);
    g.setColour (col::label);
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

    // Slot W band
    auto wband = r.removeFromTop (kWavBandH);
    g.setColour (col::window); g.fillRect (wband);
    g.setColour (col::borderSec); g.fillRect (wband.removeFromTop (1));
    auto wc = wavBandBounds().reduced (16, 0);
    wc.removeFromTop (10);
    auto wl = wc.removeFromTop (12);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText (juce::String::fromUTF8 ("SLOT W \xE2\x80\x94 WORK WAV LOOP EDITOR"), wl.removeFromLeft (220), juce::Justification::centredLeft, false);
    g.setFont (mono (11.0f, true));
    if (wavStatus_.isNotEmpty()) { g.setColour (wavStatusIsError_ ? col::warn : col::okDot); g.drawText (wavStatus_, wl, juce::Justification::centredRight, true); }
    else { g.setColour (col::text); g.drawText (wavLabel_ + (proc_.wavDirty() ? juce::String::fromUTF8 ("  \xE2\x97\x8F unsaved") : juce::String()), wl, juce::Justification::centredRight, true); }
    wc.removeFromTop (6);
    auto wctl = wc.removeFromTop (26);
    wctl.removeFromLeft (80 + 6 + 62 + 14);
    g.setFont (smallLabel()); g.setColour (col::label);
    g.drawText ("LOOP", wctl.removeFromLeft (30), juce::Justification::centredLeft, false);
    wc.removeFromTop (6 + kWaveH + 6);
    auto lower = wc.removeFromTop (kLowerH);
    lower.removeFromLeft (kSeamW + 12);
    auto labA = lower.removeFromTop (11);
    lower.removeFromTop (33 - 11 + 8);
    auto labB = lower.removeFromTop (11);
    g.setFont (smallLabel()); g.setColour (col::label);
    auto lab = [&] (juce::Rectangle<int>& row, const char* text, int w) { g.drawText (text, row.removeFromLeft (w), juce::Justification::centredLeft, false); row.removeFromLeft (8); };
    lab (labA, "LOOP START", 96); lab (labA, "LOOP END (INCL)", 96); lab (labA, "LOOP LEN", 96); lab (labA, "ROOT", 60); lab (labA, "FINE c", 60);
    lab (labB, "ATTACK ms", 60); lab (labB, "RELEASE ms", 60); lab (labB, "SAVE AS PREFIX", 130); lab (labB, "BEXT DESCRIPTION", 200);
    wc.removeFromTop (4);
    auto status = wc.removeFromTop (14);
    g.setFont (mono (10.0f)); g.setColour (col::text2);
    juce::String st;
    if (const auto* wv = proc_.wav())
    {
        const auto& ws = proc_.wavState();
        const double rate = (double) wv->sampleRate;
        if (cursorFrame_ >= 0) st << "cursor " << juce::String (cursorFrame_) << " smp  " << S (formatMs (samplesToMs ((double) cursorFrame_, rate))) << "   ";
        const juce::int64 len = (juce::int64) ws.loopEnd - (juce::int64) ws.loopStart + 1;
        const double period = rate / noteHz (ws.rootKey + ws.fineCents / 100.0);
        st << "root " << S (noteName (ws.rootKey)) << "   loop " << juce::String (len) << " smp  " << S (formatMs (samplesToMs ((double) len, rate)))
           << "  " << juce::String (len / period, 2) << " periods";
    }
    g.drawText (st, status.removeFromLeft (470), juce::Justification::centredLeft, true);
    g.setColour (col::text3);
    g.drawText (juce::String::fromUTF8 ("F/P/O loop \xC2\xB7 [ ] {} start \xC2\xB7 ; ' : \" end \xC2\xB7 wheel zoom, shift-drag pan \xC2\xB7 Ctrl+S save"), status, juce::Justification::centredRight, true);

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
    g.drawText (juce::String ("v") + ScoutProcessor::kBuildStamp, getLocalBounds().removeFromBottom (kFooterH).reduced (16, 0).removeFromLeft (isStandalone() ? 470 : 600).withTrimmedLeft (52 + 16 + 200 + 16 + 56).withTrimmedTop (2),
                juce::Justification::centredLeft, true);
}

// ------------------------------------------------------------------ list
int ScoutEditor::getNumRows()
{
    return presetRowIds_.size();
}

void ScoutEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    // F3: reads the row text copied out by rebuildForBank(), never the bank itself.
    if (row < 0 || row >= presetRowIds_.size()) return;
    if (selected) { g.setColour (col::selRow); g.fillRect (0, 0, w, h); }
    g.setColour (col::hairlineRow); g.fillRect (0, h - 1, w, 1);
    auto r = juce::Rectangle<int> (12, 0, w - 24, h - 1);
    const juce::String& id = presetRowIds_[row];
    g.setFont (mono (11.0f));
    g.setColour (selected ? col::accent : col::text3);
    const int idW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (11.0f), id));
    g.drawText (id, r.removeFromLeft (idW), juce::Justification::centredLeft, false);
    r.removeFromLeft (10);
    g.setFont (sans (13.0f)); g.setColour (col::text);
    g.drawText (presetRowNames_[row], r, juce::Justification::centredLeft, true);
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
    juce::String filter = "*.sf2;*.SF2";
    for (const auto& e : ModuleSource::supportedExtensions()) filter << ";*." << juce::String (e);
    chooser_ = std::make_unique<juce::FileChooser> ("Load a SoundFont or tracker module", juce::File(), filter);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) loadFile (f);
        });
}

bool ScoutEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".sf2") || f.endsWithIgnoreCase (".wav") || isModuleName (f)) return true;
    return false;
}

void ScoutEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // route by extension: .sf2 -> Slot R, .wav / tracker module -> Slot W (first of each)
    bool gotSf2 = false, gotWav = false;
    for (auto& f : files)
    {
        if (! gotSf2 && f.endsWithIgnoreCase (".sf2")) { loadFile (juce::File (f)); gotSf2 = true; }
        else if (! gotWav && f.endsWithIgnoreCase (".wav")) { loadWavFile (juce::File (f)); gotWav = true; }
        else if (! gotWav && isModuleName (f)) { loadModuleFile (juce::File (f)); gotWav = true; }
    }
}

// ------------------------------------------------------------------ Slot W
void ScoutEditor::chooseWav()
{
    juce::String filter = "*.wav;*.WAV";
    for (const auto& e : ModuleSource::supportedExtensions()) filter << ";*." << juce::String (e);
    const juce::File start = proc_.wavFilePath().isNotEmpty() ? juce::File (proc_.wavFilePath()).getParentDirectory()
                           : proc_.modulePath().isNotEmpty() ? juce::File (proc_.modulePath()).getParentDirectory() : juce::File();
    chooser_ = std::make_unique<juce::FileChooser> ("Load a WAV or tracker module into Slot W", start, filter);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) loadWavFile (f);
        });
}

void ScoutEditor::loadWavFile (const juce::File& f)
{
    if (isModuleName (f.getFullPathName())) { loadModuleFile (f); return; }
    const juce::String err = proc_.loadWav (f);
    if (err.isNotEmpty()) { setWavStatus ("ERROR: " + err, true); return; }
    setWavStatus ({}, false);
    rebuildForWav();
    grabKeyboardFocus();
}

void ScoutEditor::setWavStatus (const juce::String& msg, bool isError)
{
    wavStatus_ = msg; wavStatusIsError_ = isError;
    repaint (wavBandBounds());
}

juce::Rectangle<int> ScoutEditor::wavBandBounds() const
{
    return juce::Rectangle<int> (0, kHeaderH + kBodyH + kZoneBandH, kWidth, kWavBandH);
}

void ScoutEditor::rebuildForWav()
{
    seenWavGeneration_ = proc_.wavGeneration();
    if (playButton_.getToggleState()) playButton_.setToggleState (false, juce::dontSendNotification);
    const WavSample* w = proc_.wav();
    // one shared mono display copy for both views (nothing points into the engine-owned sample)
    if (w != nullptr)
    {
        auto buf = std::make_shared<std::vector<float>> (w->frames);
        const float* L = w->L(); const float* R = w->R();
        const bool st = w->isStereo();
        for (uint32_t i = 0; i < w->frames; ++i) (*buf)[i] = st ? 0.5f * (L[i] + R[i]) : L[i];
        display_ = buf;
        if (const ModuleSource* m = proc_.module())
        {
            const int idx = proc_.moduleSampleIndex();
            const juce::String nm = idx >= 1 ? juce::String::fromUTF8 (m->samples()[(size_t) idx - 1].name.c_str()) : juce::String();
            wavLabel_ = juce::String::fromUTF8 (m->fileName().c_str()) + juce::String::fromUTF8 (" \xE2\x96\xB8 ") + juce::String::formatted ("%02d ", idx) + nm
                      + juce::String::fromUTF8 ("  \xE2\x96\xBE");
        }
        else if (proc_.wavIsDecoded())
            wavLabel_ = proc_.loadedFileName() + juce::String::fromUTF8 (" \xE2\x96\xB8 ") + juce::String::fromUTF8 (w->fileName.c_str());
        else
            wavLabel_ = juce::File (proc_.wavFilePath()).getFileName();
    }
    else
    {
        display_.reset();
        wavLabel_ = juce::String::fromUTF8 ("\xE2\x80\x94 no wav loaded \xE2\x80\x94");
    }
    const double rate = w != nullptr ? (double) w->sampleRate : 44100.0;
    waveform_.setBuffer (display_, rate);
    seam_.setBuffer (display_, rate);
    prefixEditor_.setText (proc_.wavState().prefix, false);
    descEditor_.setText (proc_.wavState().description, false);
    exportToggle_.setToggleState (proc_.wavState().export16BitMono, juce::dontSendNotification);
    shownWavNote_ = -2;
    unloadWavButton_.setEnabled (w != nullptr);
    unloadWavButton_.setAlpha (w != nullptr ? 1.0f : 0.4f);
    syncWavControls();
    refreshReadout (true);
    repaint();
}

void ScoutEditor::updateFocusEnables()
{
    const bool sf2 = proc_.hasSf2(), wav = proc_.hasWav();
    focusSwitch_.setSegmentEnabled (0, sf2 || ! wav);       // with nothing loaded SF2 stays clickable (the default)
    focusSwitch_.setSegmentEnabled (1, wav);
    focusSwitch_.setSegmentEnabled (2, sf2 && wav);
    focusSwitch_.setIndex (proc_.wavState().focus, false);
    splitField_.setEnabled (sf2 && wav);
    splitField_.setAlpha (sf2 && wav ? 1.0f : 0.5f);
}

// ------------------------------------------------------------------ UNLOAD
void ScoutEditor::unloadSf2()
{
    if (! proc_.hasSf2()) return;
    proc_.unloadSoundFont();
    errorText_.clear();
    rebuildForBank();
}

void ScoutEditor::unloadWav (bool askIfDirty)
{
    if (! proc_.hasWav()) return;
    if (askIfDirty && proc_.wavDirty())
    {
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                        .withTitle ("Unsaved loop markers")
                        .withMessage (juce::File (proc_.wavFilePath()).getFileName() + " has marker edits that are not saved.")
                        .withButton ("Save").withButton ("Discard").withButton ("Cancel")
                        .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [this] (int result)
        {
            if (result == 1)                                   // Save (then unload)
            {
                if (proc_.wavState().export16BitMono) { doSaveAs(); return; }   // async save-as: leave the slot loaded
                doSave();
                if (proc_.wavDirty()) return;                  // save failed: keep the slot
                unloadWav (false);
            }
            else if (result == 2) unloadWav (false);           // Discard
        });
        return;
    }
    if (playButton_.getToggleState()) { proc_.auditionWav (kPlayNote, false); playButton_.setToggleState (false, juce::dontSendNotification); }
    proc_.unloadWav();
    setWavStatus ({}, false);
    rebuildForWav();
}

void ScoutEditor::syncWavControls()
{
    const auto& s = proc_.wavState();
    startField_.setValue ((double) s.loopStart);
    endField_.setValue ((double) s.loopEnd);
    lenField_.setValue ((double) s.loopEnd - (double) s.loopStart + 1.0);
    rootField_.setValue (s.rootKey);
    fineField_.setValue (s.fineCents);
    attackField_.setValue (s.attackMs);
    releaseField_.setValue (s.releaseMs);
    splitField_.setValue (s.splitNote);
    loopModeSwitch_.setIndex (s.loopMode, false);
    updateFocusEnables();
    waveform_.setLoop ((juce::int64) s.loopStart, (juce::int64) s.loopEnd);
    waveform_.setLoopEnabled (s.loopMode != 2);
    seam_.setLoop ((juce::int64) s.loopStart, (juce::int64) s.loopEnd);
    refreshReadout (true);
    repaint (wavBandBounds().removeFromTop (10 + 12 + 6));
    repaint (wavBandBounds().removeFromBottom (8 + 14));
}

void ScoutEditor::nudge (bool endMarker, juce::int64 delta)
{
    if (proc_.wav() == nullptr) return;
    editWav ([endMarker, delta] (WavEditState& w)
    {
        if (endMarker) w.loopEnd   = (uint32_t) juce::jmax<juce::int64> ((juce::int64) w.loopStart, (juce::int64) w.loopEnd + delta);
        else           w.loopStart = (uint32_t) juce::jlimit<juce::int64> (0, (juce::int64) w.loopEnd, (juce::int64) w.loopStart + delta);
    });
}

bool ScoutEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { doSave(); return true; }
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { doSaveAs(); return true; }
    if (proc_.module() != nullptr)
    {
        if (k.getTextCharacter() == ',') { stepModuleSample (-1); return true; }
        if (k.getTextCharacter() == '.') { stepModuleSample (+1); return true; }
    }
    if (proc_.wav() == nullptr) return false;
    switch (k.getTextCharacter())
    {
        case 'f': case 'F': editWav ([] (WavEditState& w) { w.loopMode = 0; }); return true;
        case 'p': case 'P': editWav ([] (WavEditState& w) { w.loopMode = 1; }); return true;
        case 'o': case 'O': editWav ([] (WavEditState& w) { w.loopMode = 2; }); return true;
        case '[': nudge (false, -1);   return true;
        case ']': nudge (false, +1);   return true;
        case '{': nudge (false, -100); return true;
        case '}': nudge (false, +100); return true;
        case ';': nudge (true, -1);    return true;
        case '\'': nudge (true, +1);   return true;
        case ':': nudge (true, -100);  return true;
        case '"': nudge (true, +100);  return true;
        default: break;
    }
    return false;
}

void ScoutEditor::doSave()
{
    if (proc_.wav() == nullptr) { setWavStatus ("no WAV loaded", true); return; }
    // the export checkbox never silently degrades the source: it routes SAVE to SAVE AS
    if (proc_.wavState().export16BitMono) { doSaveAs(); return; }
    // a decoded module sample / SF2 zone has no file to write back into: SAVE AS
    if (proc_.wavIsDecoded()) { doSaveAs(); return; }
    const juce::File target (proc_.wavFilePath());
    const juce::String err = proc_.saveWav (target);
    if (err.isNotEmpty()) setWavStatus ("ERROR: " + err, true);
    else setWavStatus ("saved " + target.getFileName() + "  (smpl " + juce::String ((int) proc_.wavState().loopStart) + ".." + juce::String ((int) proc_.wavState().loopEnd) + ")", false);
}

void ScoutEditor::doSaveAs()
{
    if (proc_.wav() == nullptr) { setWavStatus ("no WAV loaded", true); return; }
    chooser_ = std::make_unique<juce::FileChooser> ("Save WAV with loop markers", proc_.wavSaveAsSuggestion(), "*.wav");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.getFullPathName().isEmpty()) return;
            if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
            const juce::String err = proc_.saveWav (f);
            if (err.isNotEmpty()) setWavStatus ("ERROR: " + err, true);
            else { setWavStatus ("saved " + f.getFileName(), false); wavLabel_ = juce::File (proc_.wavFilePath()).getFileName(); }
        });
}

void ScoutEditor::loadFile (const juce::File& f)
{
    if (isModuleName (f.getFullPathName())) { loadModuleFile (f); return; }
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
    const auto* b = proc_.bank();
    fileLabel_ = b != nullptr ? proc_.loadedFileName() : juce::String::fromUTF8 ("\xE2\x80\x94 no file loaded \xE2\x80\x94");
    // F3: copy the row text now, on the message thread, while the bank is live --
    // paintListBoxItem must never dereference proc_.bank() itself.
    presetRowIds_.clear();
    presetRowNames_.clear();
    if (b != nullptr)
        for (const auto& p : b->presets())
        {
            presetRowIds_.add (InfoReadout::presetId (p));
            presetRowNames_.add (S (p.name));
        }
    presetList_.updateContent();
    if (getNumRows() > 0) presetList_.selectRow (proc_.presetIndex(), true, true);
    else presetList_.deselectAllRows();
    unloadSf2Button_.setEnabled (b != nullptr);
    unloadSf2Button_.setAlpha (b != nullptr ? 1.0f : 0.4f);
    toWavButton_.setEnabled (b != nullptr);
    toWavButton_.setAlpha (b != nullptr ? 1.0f : 0.4f);
    updateFocusEnables();
    shownNote_ = -1;
    seenNoteSeq_ = proc_.engine().lastNote().sequence;
    refreshReadout (true);
    repaint();
}

// ------------------------------------------------------------------ MIDI device (standalone)
void ScoutEditor::refreshMidiDevices (bool force)
{
    juce::StringArray ids { juce::String() }, names { "ALL MIDI INPUTS" };
    for (const auto& d : juce::MidiInput::getAvailableDevices()) { ids.add (d.identifier); names.add (d.name); }
    if (! force && ids == midiDeviceIds_) return;             // nothing plugged or unplugged
    midiDeviceIds_ = ids;
    midiDeviceCombo_.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) midiDeviceCombo_.addItem (names[i], i + 1);
    const int row = juce::jmax (0, ids.indexOf (proc_.midiInputDevice()));   // a vanished device falls back to ALL
    midiDeviceCombo_.setSelectedItemIndex (row, juce::dontSendNotification);
    applyMidiDevice();                                          // re-enable after a replug
}

void ScoutEditor::applyMidiDevice()
{
   #if JucePlugin_Build_Standalone
    // JUCE's standalone enables NO MIDI inputs by default (why a keyboard is
    // silent out of the box). Enable the chosen one, or all when none is chosen;
    // the holder already forwards every enabled input to processBlock.
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        const juce::String chosen = proc_.midiInputDevice();
        const bool all = chosen.isEmpty() || ! midiDeviceIds_.contains (chosen);
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            holder->deviceManager.setMidiInputDeviceEnabled (d.identifier, all || d.identifier == chosen);
    }
   #endif
}

// ------------------------------------------------------------------ polling
void ScoutEditor::refreshReadout (bool force)
{
    const auto info = proc_.engine().lastNote();
    const auto* bank = proc_.bank();
    const int mode = juce::roundToInt (proc_.apvts().getRawParameterValue (ParamId::mode)->load());
    const double playhead = proc_.engine().lastPlayhead();
    const bool newNote = info.sequence != seenNoteSeq_;
    const int wavNote = proc_.engine().lastWavNote();
    if (! force && ! newNote && shownPlayhead_ == playhead && shownMode_ == mode && shownWavNote_ == wavNote) return;
    shownWavNote_ = wavNote;

    ReadoutState s;
    s.presetIndex = proc_.presetIndex();
    s.mode = (PlayMode) mode;
    s.playhead = playhead;
    if (newNote) { shownNote_ = info.note; shownPreset_ = info.presetIndex; seenNoteSeq_ = info.sequence; }
    s.note = shownNote_;
    // F3: everything below reads `bank` synchronously right here (message thread,
    // loads also happen on the message thread post-F2) and copies by value --
    // nothing keeps a pointer into the bank past this function.
    s.presetLabel = dash();
    if (bank != nullptr && s.presetIndex >= 0 && s.presetIndex < bank->presetCount())
    {
        const Preset& p = bank->presets()[(size_t) s.presetIndex];
        s.presetLabel = InfoReadout::presetId (p) + "  " + S (p.name);
    }
    // The zone described is resolved on the message thread from the bank the UI holds.
    // A note played on a previous preset keeps describing that preset's zone until the next note.
    if (bank != nullptr && shownNote_ >= 0)
    {
        const int pi = (shownPreset_ >= 0 && shownPreset_ < bank->presetCount()) ? shownPreset_ : s.presetIndex;
        if (newNote && info.zoneIndex >= 0 && pi < bank->presetCount() && info.zoneIndex < (int) bank->presets()[(size_t) pi].zones.size())
            s.zone = bank->presets()[(size_t) pi].zones[(size_t) info.zoneIndex];
        else if (const Zone* zp = bank->zoneForKey (pi, shownNote_))
            s.zone = *zp;
        if (newNote) shownPreset_ = pi;
    }
    if (const WavSample* w = proc_.wav())
    {
        const auto& ws = proc_.wavState();
        ReadoutState::WavInfo wi;
        wi.file = juce::File (proc_.wavFilePath()).getFileName();
        wi.root = ws.rootKey; wi.cents = ws.fineCents;
        wi.loopStart = (juce::int64) ws.loopStart; wi.loopEnd = (juce::int64) ws.loopEnd; wi.mode = ws.loopMode;
        wi.frames = (juce::int64) w->frames; wi.rate = (double) w->sampleRate;
        wi.lastNote = wavNote; wi.stereo = w->isStereo(); wi.bits = w->bitsPerSample; wi.isFloat = w->formatTag == 3;
        s.wav = wi;
    }
    shownPlayhead_ = playhead;
    shownMode_ = mode;
    readout_.update (s);
    // the strip follows the active (selected) preset, the readout the last note
    zoneStrip_.update (bank, proc_.presetIndex(), shownNote_);
    zoneLabels_.update (zoneStrip_);
    if (force || newNote) repaint (getLocalBounds().removeFromTop (kHeaderH + kBodyH + kZoneBandH).removeFromBottom (kZoneBandH).removeFromTop (30));
}

void ScoutEditor::timerCallback()
{
    if (proc_.bankGeneration() != seenBankGeneration_) rebuildForBank();
    if (proc_.wavGeneration() != seenWavGeneration_) rebuildForWav();
    if (isStandalone() && ++midiPollTicks_ >= 30) { midiPollTicks_ = 0; refreshMidiDevices (false); }
    const double wph = proc_.engine().lastWavPlayhead();
    if (wph != shownWavPlayhead_)
    {
        shownWavPlayhead_ = wph;
        waveform_.setPlayhead (wph);
        seam_.setPlayhead (wph);
    }
    refreshReadout (false);
    const int v = proc_.engine().activeVoiceCount();
    if (v != shownVoices_) { shownVoices_ = v; repaint (getLocalBounds().removeFromBottom (kFooterH)); }
}

} // namespace sf2scout

// ------------------------------------------------------------------ v2 sources
namespace sf2scout
{

bool ScoutEditor::isModuleName (const juce::String& path)
{
    const juce::String ext = juce::File (path).getFileExtension();
    return ext.isNotEmpty() && ModuleSource::isSupportedExtension (ext.toStdString());
}

void ScoutEditor::loadModuleFile (const juce::File& f)
{
    const juce::String err = proc_.loadModule (f);
    if (err.isNotEmpty()) { setWavStatus ("ERROR: " + err, true); return; }
    const ModuleSource* m = proc_.module();
    juce::String info = juce::String::fromUTF8 (m->formatName().c_str());
    if (! m->title().empty()) info << "  \"" << juce::String::fromUTF8 (m->title().c_str()) << "\"";
    info << "  " << m->nonEmptySampleCount() << " samples  (click the name or , . to choose)";
    setWavStatus (info, false);
    rebuildForWav();
    grabKeyboardFocus();
}

juce::Rectangle<int> ScoutEditor::wavLabelBounds() const
{
    // the right-hand part of the Slot W title row (where wavLabel_ is painted)
    auto wc = wavBandBounds().reduced (16, 0);
    wc.removeFromTop (10);
    auto wl = wc.removeFromTop (12);
    wl.removeFromLeft (220);
    return wl.expanded (0, 3);
}

void ScoutEditor::mouseDown (const juce::MouseEvent& e)
{
    if (proc_.module() != nullptr && wavLabelBounds().contains (e.getPosition())) { showSamplePopup(); return; }
    juce::AudioProcessorEditor::mouseDown (e);
}

void ScoutEditor::showSamplePopup()
{
    const ModuleSource* m = proc_.module();
    if (m == nullptr) return;
    juce::PopupMenu menu;
    const int cur = proc_.moduleSampleIndex();
    for (const auto& s : m->samples())
    {
        if (s.isEmpty()) continue;
        juce::String row = juce::String::formatted ("%02d  ", s.index) + juce::String::fromUTF8 (s.name.c_str()).paddedRight (' ', 24)
                         + juce::String (s.bits) + "b" + (s.channels == 2 ? " st" : "   ")
                         + juce::String::formatted ("  %8u", (unsigned) s.frames)
                         + (s.hasSustain ? (s.sustainPingPong ? "  sus-pp" : "  sus") : s.hasLoop ? (s.pingPong ? "  pp" : "  loop") : "")
                         + "  " + juce::String ((int) s.sampleRate) + "Hz";
        menu.addItem (s.index, row, true, s.index == cur);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (wavLabelBounds())).withMinimumWidth (420),
        [this] (int result)
        {
            if (result <= 0) return;
            const juce::String err = proc_.selectModuleSample (result);
            if (err.isNotEmpty()) { setWavStatus ("ERROR: " + err, true); return; }
            setWavStatus ({}, false);
            rebuildForWav();
            grabKeyboardFocus();
        });
}

void ScoutEditor::stepModuleSample (int delta)
{
    const ModuleSource* m = proc_.module();
    if (m == nullptr) return;
    std::vector<int> order;
    for (const auto& s : m->samples()) if (! s.isEmpty()) order.push_back (s.index);
    if (order.empty()) return;
    int pos = 0;
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == proc_.moduleSampleIndex()) pos = (int) i;
    const int n = (int) order.size();
    const int next = ((pos + delta) % n + n) % n;
    const juce::String err = proc_.selectModuleSample (order[(size_t) next]);
    if (err.isNotEmpty()) { setWavStatus ("ERROR: " + err, true); return; }
    setWavStatus ({}, false);
    rebuildForWav();
}

void ScoutEditor::sendZoneToWav()
{
    const SoundFontBank* b = proc_.bank();
    if (b == nullptr) { showError ("no SoundFont loaded"); return; }
    const int pi = proc_.presetIndex();
    if (pi < 0 || pi >= b->presetCount()) return;
    const auto& zones = b->presets()[(size_t) pi].zones;
    if (zones.empty()) { showError ("preset has no zones"); return; }
    // the zone the readout describes: last played zone of this preset, else the zone under the last note, else the first
    int zi = -1;
    const LastNoteInfo info = proc_.engine().lastNote();
    if (info.presetIndex == pi && info.zoneIndex >= 0 && info.zoneIndex < (int) zones.size()) zi = info.zoneIndex;
    if (zi < 0 && shownNote_ >= 0)
        for (size_t i = 0; i < zones.size(); ++i) if (zones[i].coversKey (shownNote_)) { zi = (int) i; break; }
    if (zi < 0) zi = 0;
    if (proc_.wavDirty())
    {
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                        .withTitle ("Unsaved loop markers")
                        .withMessage ("Slot W has marker edits that are not saved. Replace it with the SF2 zone?")
                        .withButton ("Replace").withButton ("Cancel").withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [this, pi, zi] (int r) { if (r == 1) { proc_.setWavState (proc_.wavState()); const auto e = proc_.sendZoneToWav (pi, zi); if (e.isNotEmpty()) setWavStatus ("ERROR: " + e, true); else { setWavStatus ({}, false); rebuildForWav(); } } });
        return;
    }
    const juce::String err = proc_.sendZoneToWav (pi, zi);
    if (err.isNotEmpty()) { setWavStatus ("ERROR: " + err, true); return; }
    setWavStatus ("zone " + juce::String (zi + 1) + " of preset " + juce::String (pi + 1) + " decoded into Slot W (SAVE AS to export)", false);
    rebuildForWav();
    grabKeyboardFocus();
}

} // namespace sf2scout
