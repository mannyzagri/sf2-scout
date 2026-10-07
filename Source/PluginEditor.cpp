#include "PluginEditor.h"
#include "engine/NoteNames.h"
#include "engine/Assists.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace sf2scout
{
using namespace ui;

namespace
{
    juce::String S (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    juce::String nn (int n) { return S (noteName (n)); }
    juce::String msOf (double frames, double rate) { return S (formatMs (samplesToMs (frames, rate))); }
    juce::String kindName (int k, bool sustain) { return sustain ? "forward + sustain loop" : k == 1 ? "ping-pong" : k == 2 ? "off (one-shot)" : "forward"; }
    const char* kindShort (int k) { return k == 1 ? "pp" : k == 2 ? "off" : k == 3 ? "sus" : "fwd"; }
    juce::Colour kindColour (int k) { return k == 1 ? col::accent : k == 2 ? col::text3 : k == 3 ? col::warn : col::text2; }
    juce::Colour badgeColour (SourceType t) { return t == SourceType::Sf2 ? col::badgeSf2 : t == SourceType::Module ? col::badgeMod : t == SourceType::Wav ? col::badgeWav : col::badgeErr; }
    const juce::String kMiddot = juce::String::fromUTF8 (" \xC2\xB7 ");
}

int ScoutEditor::parseNote (const juce::String& in)
{
    const juce::String s = in.trim();
    if (s.isEmpty()) return -1;
    if (s.containsOnly ("0123456789")) return juce::jlimit (0, 127, s.getIntValue());
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const juce::juce_wchar l = juce::CharacterFunctions::toUpperCase (s[0]);
    if (l < 'A' || l > 'G') return -1;
    int semi = base[l - 'A'], i = 1;
    if (i < s.length() && s[i] == '#') { ++semi; ++i; }
    else if (i < s.length() && s[i] == 'b') { --semi; ++i; }
    const juce::String oct = s.substring (i);
    if (oct.isEmpty() || ! oct.containsOnly ("-0123456789")) return -1;
    const int n = (oct.getIntValue() + 1) * 12 + semi;
    return n >= 0 && n <= 127 ? n : -1;
}

ScoutEditor::ScoutEditor (ScoutProcessor& p)
    : AudioProcessorEditor (p), proc_ (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (face_);
    face_.setBounds (0, 0, kW, kH);
    face_.setInterceptsMouseClicks (false, true);
    // window: 1200 x 840 at scale 1, uniform scale 0.75-1.5 with a fixed aspect (README CHANGES 1)
    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.75), (int) (kH * 0.75), (int) (kW * 1.5), (int) (kH * 1.5));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    const double k = proc_.uiScale();
    setSize ((int) std::lround (kW * k), (int) std::lround (kH * k));

    // ---- header
    face_.addAndMakeVisible (loadButton_);
    loadButton_.onClick = [this] { chooseSource(); };
    loadButton_.setTooltip ("open a SoundFont, a tracker module or a WAV (also: drop files anywhere)");
    face_.addAndMakeVisible (kbSeg_);
    kbSeg_.setTooltip ("which slot the MIDI keyboard plays: A, B, SPLIT (below the split note A, at/above B) or TOGGLE (TAB swaps)");
    kbSeg_.onChange = [this] (int i) { proc_.setKbMode ((KbMode) i); };
    face_.addAndMakeVisible (splitField_);
    splitField_.setTooltip (juce::String::fromUTF8 ("split note \xE2\x80\x94 keys below play A, at/above play B"));
    splitField_.setJustification (juce::Justification::centred);
    splitField_.onCommit = [this] (const juce::String& t) { const int n = parseNote (t); if (n >= 0) proc_.setSplitNote (n); splitField_.setShown (nn (proc_.splitNote())); };
    face_.addAndMakeVisible (modeSeg_);
    if (auto* modeParam = proc_.apvts().getParameter (ParamId::mode))
    {
        modeAttachment_ = std::make_unique<juce::ParameterAttachment> (*modeParam,
            [this] (float v) { modeSeg_.setIndex (juce::roundToInt (v), false); rebuildReadouts(); },
            proc_.apvts().undoManager);
        modeAttachment_->sendInitialUpdate();
        modeSeg_.onChange = [this] (int i) { modeAttachment_->setValueAsCompleteGesture ((float) i); };
    }

    // ---- body
    face_.addAndMakeVisible (sourceList_);
    sourceList_.onRowClicked = [this] (const TreeRow& r)
    {
        if (r.isError) { if (const Source* s = proc_.source (r.srcId)) setStatus ("ERROR: " + s->fileName + ": " + s->error, true); return; }
        if (r.preset >= 0) { proc_.selectPreset (r.srcId, r.preset); return; }
        if (Source* s = proc_.source (r.srcId))
        {
            if (s->type == SourceType::Sf2) s->expanded = proc_.selectedSource() == r.srcId ? ! s->expanded : true;
        }
        proc_.selectSource (r.srcId);
    };
    face_.addAndMakeVisible (unloadButton_);
    unloadButton_.setTooltip ("remove the selected source; its A/B slots are cleared (asks first if a WAV has unsaved edits)");
    unloadButton_.onClick = [this] { unloadSelected(); };
    face_.addAndMakeVisible (contents_);
    contents_.onRowClicked = [this] (const SampleKey& k) { const juce::String e = proc_.selectSample (k); if (e.isNotEmpty()) setStatus ("ERROR: " + e, true); };
    contents_.onRowDoubleClicked = [this] (const SampleKey& k)
    {
        if (proc_.selectSample (k).isEmpty() && ! proc_.latched()) { proc_.setLatched (true); }
    };
    face_.addAndMakeVisible (assignA_); assignA_.setTooltip ("send the selected sample to slot A"); assignA_.onClick = [this] { assign (kSlotA); };
    face_.addAndMakeVisible (assignB_); assignB_.setTooltip ("send the selected sample to slot B"); assignB_.onClick = [this] { assign (kSlotB); };
    face_.addAndMakeVisible (prevButton_); prevButton_.onClick = [this] { step (-1); };
    face_.addAndMakeVisible (nextButton_); nextButton_.onClick = [this] { step (+1); };
    face_.addAndMakeVisible (readoutA_);
    face_.addAndMakeVisible (readoutB_);

    // ---- zone band
    face_.addAndMakeVisible (zoneStrip_);
    face_.addAndMakeVisible (zoneLabels_);
    zoneStrip_.onKeyDown = [this] (int n) { zoneKey (n, true); };
    zoneStrip_.onKeyUp   = [this] (int n) { zoneKey (n, false); };

    // ---- editor
    face_.addAndMakeVisible (playButton_);
    playButton_.setTooltip ("latch the current sample at its root (SPACE); again to stop");
    playButton_.onClick = [this] { togglePlay(); };
    face_.addAndMakeVisible (loopSeg_);
    loopSeg_.onChange = [this] (int i) { setLoopKind (i); };
    face_.addAndMakeVisible (snapToggle_);
    snapToggle_.setToggled (true, false);
    snapToggle_.onChange = [this] (bool on) { waveform_.setSnap (on); };
    face_.addAndMakeVisible (suggestButton_);
    suggestButton_.setTooltip (juce::String::fromUTF8 ("best-splice search \xC2\xB1" "200 ms"));
    suggestButton_.onClick = [this] { doSuggest(); };
    face_.addAndMakeVisible (autoRootButton_);
    autoRootButton_.setTooltip ("autocorrelation on the loop region");
    autoRootButton_.onClick = [this] { doAutoRoot(); };
    face_.addAndMakeVisible (saveAsButton_); saveAsButton_.onClick = [this] { doSaveAs(); };
    face_.addAndMakeVisible (saveButton_);   saveButton_.onClick = [this] { doSave(); };
    face_.addAndMakeVisible (waveform_);
    face_.addAndMakeVisible (seam_);
    waveform_.onWantsFocus = [this] { grabKeyboardFocus(); };
    waveform_.onCursor = [this] (juce::int64 f) { cursorFrame_ = f; rebuildEditor(); };
    waveform_.onLoopDragged = [this] (juce::int64 a, juce::int64 b)
    {
        applyEdit ([a, b] (SampleEdits& e) { e.loopStart = (uint32_t) juce::jmax<juce::int64> (0, a); e.loopEnd = (uint32_t) juce::jmax<juce::int64> (0, b); });
    };
    struct FieldDef { NumField* f; const char* tip; };
    for (auto d : { FieldDef { &startField_, "LOOP START (samples)" }, FieldDef { &endField_, "LOOP END (samples, last sample INCLUDED)" },
                    FieldDef { &lenField_, "LOOP LENGTH (samples) -> moves LOOP END" }, FieldDef { &rootField_, "ROOT KEY (note name or MIDI number, C4 = 60)" },
                    FieldDef { &fineField_, "FINE TUNE (cents, -99..99)" }, FieldDef { &attackField_, "ATTACK fade-in (ms, 0..500)" }, FieldDef { &releaseField_, "RELEASE fade (ms, 10..5000); the loop keeps cycling under it" },
                    FieldDef { &prefixField_, "SAVE AS names the file <PREFIX>_<NOTE>.wav" }, FieldDef { &descField_, "bext Description written on export (auto-generated provenance, editable)" } })
    {
        d.f->setTooltip (d.tip);
        face_.addAndMakeVisible (*d.f);
    }
    face_.addAndMakeVisible (periods_);
    face_.addAndMakeVisible (clickMeter_);
    face_.addAndMakeVisible (loudness_);
    descField_.setTextColour (col::text2);
    startField_.onCommit   = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.loopStart = (uint32_t) juce::jmax (0, t.getIntValue()); if (e.loopEnd < e.loopStart) e.loopEnd = e.loopStart; }); else rebuildEditor(); };
    endField_.onCommit     = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.loopEnd = (uint32_t) juce::jmax (0, t.getIntValue()); if (e.loopStart > e.loopEnd) e.loopStart = e.loopEnd; }); else rebuildEditor(); };
    lenField_.onCommit     = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.loopEnd = (uint32_t) ((juce::int64) e.loopStart + juce::jmax (1, t.getIntValue()) - 1); }); else rebuildEditor(); };
    rootField_.onCommit    = [this] (const juce::String& t) { const int n = parseNote (t); if (n >= 0) applyEdit ([n] (SampleEdits& e) { e.rootKey = n; }); else rebuildEditor(); };
    fineField_.onCommit    = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.fineCents = std::round (t.getDoubleValue()); }); else rebuildEditor(); };
    attackField_.onCommit  = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.attackMs = std::round (t.getDoubleValue()); }); else rebuildEditor(); };
    releaseField_.onCommit = [this] (const juce::String& t) { if (t.isNotEmpty()) applyEdit ([&] (SampleEdits& e) { e.releaseMs = std::round (t.getDoubleValue()); }); else rebuildEditor(); };
    prefixField_.onCommit  = [this] (const juce::String& t) { applyEdit ([&] (SampleEdits& e) { e.prefix = t.trim(); }); };
    descField_.onCommit    = [this] (const juce::String& t) { applyEdit ([&] (SampleEdits& e) { e.description = t; }); };

    // ---- export column
    face_.addAndMakeVisible (exportCol_);
    exportCol_.onBrowse = [this] { browseFolder(); };
    exportCol_.onQuick = [this] (int w) { quickTarget (w); };
    exportCol_.onFmt = [this] (int i) { auto e = proc_.exportSettings(); e.convert16 = i == 1; proc_.setExportSettings (e); };
    exportCol_.onFold = [this] (int i) { auto e = proc_.exportSettings(); e.fold = i; proc_.setExportSettings (e); };
    exportCol_.onScope = [this] (int i) { auto e = proc_.exportSettings(); e.scope = i; proc_.setExportSettings (e); };
    exportCol_.onSkip = [this] (bool on) { auto e = proc_.exportSettings(); e.skipExisting = on; proc_.setExportSettings (e); };
    exportCol_.onRange = [this] (uint32_t a, uint32_t b) { auto e = proc_.exportSettings(); e.rangeStart = a; e.rangeEnd = b; proc_.setExportSettings (e); };
    exportCol_.onExportSample = [this] { exportSample(); };
    exportCol_.onExportRange = [this] { exportRange(); };
    exportCol_.onBatch = [this] { batchToggle(); };

    // ---- footer
    masterSlider_.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterSlider_.setLookAndFeel (&sliderLook_);
    face_.addAndMakeVisible (masterSlider_);
    masterAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc_.apvts(), ParamId::masterGain, masterSlider_);
    masterSlider_.onValueChange = [this] { face_.repaint (0, kH - 44, kW, 44); };
    midiCombo_.setLookAndFeel (&chipLook_);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (proc_.apvts().getParameter (ParamId::midiChannel)))
        midiCombo_.addItemList (ch->choices, 1);
    face_.addAndMakeVisible (midiCombo_);
    midiAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc_.apvts(), ParamId::midiChannel, midiCombo_);
    midiDeviceCombo_.setLookAndFeel (&chipLook_);
    midiDeviceCombo_.setTooltip ("MIDI input device for the standalone (the plugin gets MIDI from the host)");
    midiDeviceCombo_.onChange = [this]
    {
        const int row = midiDeviceCombo_.getSelectedItemIndex();
        proc_.setMidiInputDevice (row > 0 && row < midiDeviceIds_.size() ? midiDeviceIds_[row] : juce::String());
        applyMidiDevice();
    };
    face_.addChildComponent (midiDeviceCombo_);
    midiDeviceCombo_.setVisible (isStandalone());
    if (isStandalone()) { refreshMidiDevices (true); applyMidiDevice(); }

    // layout at the manifest's bounds
    loadButton_.setBounds (16, 9, 64, 30);
    kbSeg_.setBounds (637, 8, 232, 32);
    splitField_.setBounds (883, 12, 48, 24);
    modeSeg_.setBounds (999, 9, 185, 30);
    sourceList_.setBounds (0, 90, 204, 278);
    unloadButton_.setBounds (138, 58, 56, 22);
    contents_.setBounds (204, 90, 296, 278);
    assignA_.setBounds (350, 58, 34, 22);
    assignB_.setBounds (390, 58, 34, 22);
    prevButton_.setBounds (430, 58, 22, 22);
    nextButton_.setBounds (458, 58, 22, 22);
    readoutA_.setBounds (512, 60, 201, 296);
    readoutB_.setBounds (727, 60, 201, 296);
    zoneStrip_.setBounds (16, 396, 912, 52);
    zoneLabels_.setBounds (16, 452, 912, 20);
    playButton_.setBounds (16, 502, 84, 26);
    loopSeg_.setBounds (148, 502, 179, 26);
    snapToggle_.setBounds (341, 502, 110, 26);
    suggestButton_.setBounds (465, 502, 82, 26);
    autoRootButton_.setBounds (555, 502, 140, 26);
    saveAsButton_.setBounds (784, 502, 76, 26);
    saveButton_.setBounds (868, 502, 60, 26);
    waveform_.setBounds (16, 534, 912, 140);
    seam_.setBounds (16, 682, 300, 80);
    startField_.setBounds (328, 695, 84, 22);
    endField_.setBounds (420, 695, 84, 22);
    lenField_.setBounds (512, 695, 84, 22);
    periods_.setBounds (604, 695, 68, 22);
    rootField_.setBounds (680, 695, 52, 22);
    fineField_.setBounds (740, 695, 52, 22);
    attackField_.setBounds (800, 695, 56, 22);
    releaseField_.setBounds (864, 695, 56, 22);
    clickMeter_.setBounds (328, 735, 110, 22);
    loudness_.setBounds (446, 735, 84, 22);
    prefixField_.setBounds (538, 735, 110, 22);
    descField_.setBounds (656, 735, 272, 22);
    exportCol_.setBounds (945, 48, 255, 748);
    masterSlider_.setBounds (80, 808, 200, 20);
    midiDeviceCombo_.setBounds (772, 804, 150, 28);      // README 6 flex rule (see paintFace); manifest said 806
    midiCombo_.setBounds (992, 804, 76, 28);

    rebuildAll();
    startTimerHz (30);
}

ScoutEditor::~ScoutEditor()
{
    stopTimer();
    masterSlider_.setLookAndFeel (nullptr);
    midiCombo_.setLookAndFeel (nullptr);
    midiDeviceCombo_.setLookAndFeel (nullptr);
    for (int i = 0; i < 12; ++i) if (qwertyDown_[i]) proc_.auditionKeyboard (kQwertyBase + i, false);
}

// ------------------------------------------------------------------ window / scale
void ScoutEditor::resized()
{
    // uniform scale from whichever dimension the host gives less of (a host that
    // ignores the aspect constraint still gets an undistorted face, top-left anchored)
    const double k = juce::jlimit (0.5, 2.0, std::min ((double) getWidth() / (double) kW, (double) getHeight() / (double) kH));
    face_.setTransform (juce::AffineTransform::scale ((float) k));
    face_.setBounds (0, 0, kW, kH);
    proc_.setUiScale (k);
}

void ScoutEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::window);
}

// ------------------------------------------------------------------ static chrome
void ScoutEditor::paintFace (juce::Graphics& g)
{
    g.fillAll (col::window);
    // header 0/48
    g.setColour (col::headFoot); g.fillRect (0, 0, kW, 48);
    g.setColour (col::borderSec); g.fillRect (0, 47, kW, 1);
    drawSmallLabel (g, "SOURCES OPEN", { 94, 12, 250, 12 });
    g.setFont (mono (13.0f, true)); g.setColour (col::text);
    g.drawText (sourceSummary_, 94, 26, 250, 16, juce::Justification::centredLeft, true);
    drawSmallLabel (g, "KEYBOARD PLAYS", { 480, 0, 143, 48 }, col::text2, juce::Justification::centredRight);
    drawSmallLabel (g, "MODE", { 941, 0, 44, 48 }, col::label, juce::Justification::centredRight);

    // body 48/320: source list + contents headers, dividers
    g.setColour (col::borderSec); g.fillRect (0, 367, 944, 1);
    g.setColour (col::borderSec); g.fillRect (203, 48, 1, 320); g.fillRect (499, 48, 1, 320);
    g.setColour (col::hairline); g.fillRect (0, 89, 203, 1); g.fillRect (204, 89, 295, 1);
    drawSmallLabel (g, "SOURCES", { 12, 48, 120, 42 });
    drawSmallLabel (g, listKind_, { 216, 56, 128, 12 });
    g.setFont (mono (11.0f, true)); g.setColour (col::text);
    g.drawText (listTitle_, 216, 70, 128, 14, juce::Justification::centredLeft, true);

    // zone band 368/104
    g.setColour (col::zoneBand); g.fillRect (0, 368, 944, 104);
    g.setColour (col::borderSec); g.fillRect (0, 471, 944, 1);
    drawSmallLabel (g, zoneTitle_, { 16, 379, 700, 12 });
    drawSmallLabel (g, zoneCount_, { 716, 379, 212, 12 }, col::label, juce::Justification::centredRight);

    // editor 472/324: title row
    int x = 16;
    drawSmallLabel (g, "EDITOR", { x, 482, 48, 12 });
    x += (int) std::ceil (juce::GlyphArrangement::getStringWidth (smallLabel(), "EDITOR")) + 8;
    x += drawBadge (g, x, 481, editorBadge_, editorBadgeBg_, 14) + 8;
    const int statusW = status_.isEmpty() ? 0 : (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (11.0f, true), status_)) + 8;
    g.setFont (mono (11.0f, true)); g.setColour (col::text);
    g.drawText (editorTitle_, x, 481, juce::jmax (40, 928 - x - statusW), 14, juce::Justification::centredLeft, true);
    g.setColour (statusWarn_ ? col::warn : col::okDot);
    g.drawText (status_, 16, 481, 912, 14, juce::Justification::centredRight, true);
    drawSmallLabel (g, "LOOP", { 114, 502, 30, 26 });
    drawSmallLabel (g, "LOOP START", { 328, 682, 84, 11 });
    drawSmallLabel (g, "LOOP END (INCL)", { 420, 682, 92, 11 });
    drawSmallLabel (g, "LOOP LEN", { 512, 682, 84, 11 });
    drawSmallLabel (g, "PERIODS", { 604, 682, 68, 11 });
    drawSmallLabel (g, "ROOT", { 680, 682, 52, 11 });
    drawSmallLabel (g, "FINE c", { 740, 682, 52, 11 });
    drawSmallLabel (g, "ATTACK ms", { 800, 682, 64, 11 });
    drawSmallLabel (g, "RELEASE ms", { 864, 682, 70, 11 });
    drawSmallLabel (g, "CLICK METER", { 328, 722, 110, 11 });
    drawSmallLabel (g, "LOUDNESS", { 446, 722, 84, 11 });
    drawSmallLabel (g, "SAVE AS PREFIX", { 538, 722, 110, 11 });
    drawSmallLabel (g, "BEXT DESCRIPTION (PROVENANCE, AUTO)", { 656, 722, 272, 11 });
    g.setFont (mono (10.0f)); g.setColour (col::text2);
    g.drawText (cursorLine_, 16, 770, 470, 14, juce::Justification::centredLeft, true);
    g.setColour (col::text3);
    g.drawText (juce::String::fromUTF8 ("F/P/O loop \xC2\xB7 [ ] { } start \xC2\xB7 ; ' : \" end \xC2\xB7 TAB swap A/B \xC2\xB7 , . sample \xC2\xB7 SPACE latch \xC2\xB7 Ctrl+S save"),
                486, 770, 442, 14, juce::Justification::centredRight, true);

    // footer 796/44
    g.setColour (col::headFoot); g.fillRect (0, 796, kW, 44);
    g.setColour (col::borderSec); g.fillRect (0, 796, kW, 1);
    drawSmallLabel (g, "MASTER", { 16, 796, 52, 44 });
    const double gain = masterSlider_.getValue();
    g.setFont (mono (12.0f, true)); g.setColour (col::text);
    g.drawText (gain > 0.0 ? juce::String (20.0 * std::log10 (gain), 1) + " dB" : juce::String ("-inf dB"), 292, 796, 56, 44, juce::Justification::centredLeft, false);
    g.setFont (mono (9.0f)); g.setColour (col::text3);
    g.drawText (juce::String ("v") + ScoutProcessor::kBuildStamp, 360, 796, 120, 44, juce::Justification::centredLeft, true);
    // README 6 flex rule (gap 12, right-anchored at 1184): voices 104 · chip 76 · "MIDI IN" · DEVICE chip 150 · "DEVICE" · hint.
    // The manifest's deviceChip x (806) contradicts that rule (it would overlap "MIDI IN"); the rule wins, reported upstream.
    g.setFont (mono (10.0f)); g.setColour (col::text3);
    const int hintRight = isStandalone() ? 688 : 922;
    g.drawText (juce::String::fromUTF8 ("QWERTY piano Z\xE2\x80\x93M \xC2\xB7 SPACE latch"), hintRight - 220, 796, 220, 44, juce::Justification::centredRight, false);
    if (isStandalone()) drawSmallLabel (g, "DEVICE", { 700, 796, 60, 44 }, col::label, juce::Justification::centredRight);
    drawSmallLabel (g, "MIDI IN", { 924, 796, 56, 44 }, col::label, juce::Justification::centredRight);
    const int n = proc_.engine().activeVoiceCount();
    const juce::String voices = juce::String (n) + "/" + juce::String (ScoutEngine::kMaxVoices) + " voices";
    g.setFont (mono (12.0f)); g.setColour (col::text2);
    const int vw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (12.0f), voices));
    g.drawText (voices, 1080, 796, 104, 44, juce::Justification::centredRight, false);
    g.setColour (n > 0 ? col::okDot : col::idleDot);
    g.fillEllipse ((float) (1184 - vw - 6 - 8), 814.0f, 8.0f, 8.0f);
}

// ------------------------------------------------------------------ rebuild
void ScoutEditor::rebuildAll()
{
    seenGeneration_ = proc_.sourceGeneration();
    rebuildHeader();
    rebuildLists();
    rebuildReadouts();
    rebuildZoneMap();
    rebuildEditor();
    rebuildExport();
    face_.repaint();
}

void ScoutEditor::rebuildHeader()
{
    juce::StringArray names;
    for (const auto& s : proc_.sources()) if (s->type != SourceType::Error) names.add (s->fileName);
    sourceSummary_ = names.isEmpty() ? juce::String::fromUTF8 ("\xE2\x80\x94 no source open \xE2\x80\x94") : names.joinIntoString (kMiddot);
    const bool a = proc_.slotKey (kSlotA).valid(), b = proc_.slotKey (kSlotB).valid();
    kbSeg_.setSegmentEnabled (0, a || ! b);
    kbSeg_.setSegmentEnabled (1, b);
    kbSeg_.setSegmentEnabled (2, a && b);
    kbSeg_.setSegmentEnabled (3, a && b);
    kbSeg_.setIndex ((int) proc_.kbMode(), false);
    splitField_.setShown (nn (proc_.splitNote()));
    splitField_.setAlpha (proc_.kbMode() == KbMode::Split ? 1.0f : 0.45f);
}

void ScoutEditor::rebuildLists()
{
    const int sel = proc_.selectedSource();
    std::vector<TreeRow> rows;
    for (const auto& s : proc_.sources())
    {
        TreeRow r;
        r.srcId = s->id; r.pad = 8;
        r.isError = s->type == SourceType::Error;
        r.caret = s->type == SourceType::Sf2 ? juce::String::fromUTF8 (s->expanded ? "\xE2\x96\xBE" : "\xE2\x96\xB8") : (r.isError ? juce::String ("!") : juce::String::fromUTF8 ("\xE2\x97\x8F"));
        r.badge = proc_.typeBadge (*s);
        r.badgeBg = badgeColour (s->type);
        r.label = r.isError ? s->fileName + juce::String::fromUTF8 (" \xE2\x80\x94 ") + s->error : s->fileName;
        r.sub = s->type == SourceType::Sf2 ? juce::String (s->sampleCount()) + " presets"
              : s->type == SourceType::Module ? juce::String (s->sampleCount()) + " smp"
              : s->type == SourceType::Wav && s->wav != nullptr ? juce::String (s->wav->bitsPerSample) + "b " + (s->wav->isStereo() ? "st" : "mo") : juce::String();
        r.medium = ! r.isError;
        r.fg = r.isError ? col::badgeErr : col::text;
        r.selected = sel == s->id && ! r.isError;
        rows.push_back (r);
        if (s->type == SourceType::Sf2 && s->expanded && s->bank != nullptr)
        {
            for (int p = 0; p < s->bank->presetCount(); ++p)
            {
                const Preset& pr = s->bank->presets()[(size_t) p];
                TreeRow q;
                q.srcId = s->id; q.preset = p; q.pad = 26;
                q.badge = juce::String (pr.bank).paddedLeft ('0', 3) + ":" + juce::String (pr.program).paddedLeft ('0', 3);
                q.badgeBg = col::text3;
                q.label = S (pr.name);
                q.sub = juce::String ((int) pr.zones.size()) + " zn";
                q.selected = sel == s->id && s->selPreset == p;
                rows.push_back (q);
            }
        }
    }
    sourceList_.setRows (std::move (rows));

    listTitle_ = proc_.contentsTitle (sel, listKind_);
    std::vector<ListRow> lr;
    const SampleKey c = cur(), a = proc_.slotKey (kSlotA), b = proc_.slotKey (kSlotB);
    for (const auto& row : proc_.contents (sel))
    {
        ListRow l;
        l.key = row.key;
        l.idx = row.index < 10 ? "0" + juce::String (row.index) : juce::String (row.index);
        l.name = row.lo >= 0 ? row.name + "  " + nn (row.lo) + enDash() + nn (row.hi) : row.name;
        l.fmt = juce::String (row.bits) + "b " + (row.channels == 2 ? "st" : "mo");
        l.frames = juce::String ((int) row.frames);
        l.kind = kindShort (row.loopKind);
        l.kindColour = kindColour (row.loopKind);
        l.rate = juce::String ((int) row.rate);
        l.selected = row.key == c;
        l.inA = row.key == a; l.inB = row.key == b;
        lr.push_back (l);
    }
    juce::String emptyMsg = "No source selected.";
    if (const Source* s = proc_.source (sel))
        if (s->type == SourceType::Error) emptyMsg = "ERROR: " + s->fileName + ": " + s->error + ". Previous state kept.";
    contents_.setRows (std::move (lr), emptyMsg);
    if (c.valid()) contents_.scrollToKey (c);
}

void ScoutEditor::rebuildReadouts()
{
    for (int slot = kSlotA; slot <= kSlotB; ++slot)
    {
        ReadoutData d;
        d.slot = slot == kSlotA ? "A" : "B";
        d.slotBg = slot == kSlotA ? col::slotA : col::slotB;
        d.borderRight = slot == kSlotA;
        d.live = proc_.slotLive (slot);
        const SampleKey key = proc_.slotKey (slot);
        const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
        const SampleEdits* e = key.valid() ? proc_.edits (key) : nullptr;
        if (w == nullptr || e == nullptr)
        {
            d.empty = true;
            d.path = "slot " + d.slot + " empty";
            d.emptyMsg = juce::String::fromUTF8 ("Select a sample and press \xE2\x86\x92 ") + d.slot
                       + juce::String::fromUTF8 (". With both slots set, KEYBOARD PLAYS offers SPLIT (below the split note \xE2\x86\x92 A, at/above \xE2\x86\x92 B) and TOGGLE (TAB swaps).");
        }
        else
        {
            d.empty = false;
            const SourceType t = proc_.typeOf (key);
            d.badge = proc_.typeBadge (key);
            d.badgeBg = badgeColour (t);
            d.path = proc_.pathOf (key);
            const int ln = proc_.engine().lastNote (slot);
            const int st = ln >= 0 ? ln - e->rootKey : 0;
            d.played = ln >= 0 ? nn (ln) : dash();
            d.root = nn (e->rootKey);
            d.fine = std::fabs (e->fineCents) > 0.5 ? juce::String::formatted ("%+dc", (int) std::lround (e->fineCents)) : juce::String();
            d.stretch = ln >= 0 ? juce::String (st > 0 ? "+" : "") + juce::String (st) + " st" : dash();
            d.stretchColour = std::abs (st) > 7 ? col::warn : col::text;
            const double rate = (double) w->sampleRate;
            const uint32_t len = e->loopEnd >= e->loopStart ? e->loopEnd - e->loopStart + 1 : 0;
            const double per = loopPeriods (len, rate, e->rootKey, e->fineCents);
            const bool ni = periodsNearInteger (per);
            const Source* src = proc_.source (key.src);
            int srcBits = w->bitsPerSample;
            if (src != nullptr && src->type == SourceType::Module && src->module != nullptr && key.a >= 1 && key.a <= src->module->sampleCount())
                srcBits = src->module->samples()[(size_t) key.a - 1].bits;
            d.rows.push_back ({ "source", (src != nullptr ? src->fileName : juce::String()) + " (" + d.badge.toLowerCase() + ")", col::text });
            d.rows.push_back ({ "format", juce::String ((int) rate) + " Hz  " + juce::String (srcBits) + (w->formatTag == 3 ? "f  " : "-bit  ") + (w->isStereo() ? "stereo" : "mono"), col::text });
            d.rows.push_back ({ "length", juce::String ((int) w->frames) + "  " + msOf (w->frames, rate), col::text });
            d.rows.push_back ({ "loopStart", juce::String ((int) e->loopStart) + "  " + msOf (e->loopStart, rate), col::text });
            d.rows.push_back ({ "loopEnd", juce::String ((int) e->loopEnd) + "  " + msOf (e->loopEnd, rate), col::text });
            d.rows.push_back ({ "loop len", juce::String ((int) len) + "  " + msOf (len, rate) + "  " + juce::String (per, 2) + " per", ni ? col::text : col::warn });
            d.rows.push_back ({ "loop type", kindName (e->loopKind, e->sourceKind == 3 && ! e->loopOverride) + (e->loopOverride ? " (override)" : ""), col::text });
            if (t == SourceType::Sf2 && src != nullptr && src->bank != nullptr && key.a < src->bank->presetCount())
            {
                const auto& zones = src->bank->presets()[(size_t) key.a].zones;
                if (key.b >= 0 && key.b < (int) zones.size())
                {
                    const Zone& z = zones[(size_t) key.b];
                    d.rows.push_back ({ "zone", nn (z.lokey) + enDash() + nn (z.hikey) + "  vel " + juce::String (juce::jmax (1, z.lovel)) + enDash() + juce::String (z.hivel), col::text });
                }
            }
            else
                d.rows.push_back ({ "loudness", juce::String (loopRmsDb (*w, e->loopStart, e->loopEnd), 1) + " dB RMS", col::text });
        }
        (slot == kSlotA ? readoutA_ : readoutB_).set (d);
    }
}

void ScoutEditor::rebuildZoneMap()
{
    sfSlot_ = -1;
    for (int slot = kSlotA; slot <= kSlotB && sfSlot_ < 0; ++slot)
        if (proc_.typeOf (proc_.slotKey (slot)) == SourceType::Sf2) sfSlot_ = slot;
    std::vector<ZoneCell> cells;
    juce::String presetName;
    if (sfSlot_ >= 0)
    {
        const SampleKey key = proc_.slotKey (sfSlot_);
        if (const Source* s = proc_.source (key.src))
            if (s->bank != nullptr && key.a >= 0 && key.a < s->bank->presetCount())
            {
                const Preset& p = s->bank->presets()[(size_t) key.a];
                presetName = S (p.name);
                for (const Zone& z : p.zones) cells.push_back ({ z.lokey, z.hikey, z.rootKey, S (z.sampleName) });
            }
    }
    const int split = proc_.kbMode() == KbMode::Split ? proc_.splitNote() : -1;
    zoneStrip_.setZones (cells, sfSlot_ >= 0 ? lastSf2Note_ : -1, sfSlot_ >= 0 ? split : -1);
    zoneLabels_.setZones (cells);
    if (sfSlot_ >= 0)
    {
        zoneTitle_ = juce::String::fromUTF8 ("ZONE MAP \xE2\x80\x94 ") + (sfSlot_ == kSlotA ? "A" : "B") + kMiddot + presetName + juce::String::fromUTF8 (" \xE2\x80\x94 CLICK A KEY TO AUDITION");
        zoneCount_ = juce::String ((int) cells.size()) + " ZONES" + (split >= 0 ? kMiddot + "SPLIT " + nn (split) : juce::String());
    }
    else
    {
        zoneTitle_ = juce::String::fromUTF8 ("ZONE MAP \xE2\x80\x94 NO SF2 IN A/B");
        zoneCount_ = dash();
    }
}

void ScoutEditor::rebuildEditor()
{
    const SampleKey key = cur();
    const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
    const SampleEdits* e = key.valid() ? proc_.edits (key) : nullptr;
    const bool have = w != nullptr && e != nullptr;
    // display buffer: one mono copy per sample, rebuilt only when the sample changes
    if (! have) { display_.reset(); displayKey_ = {}; }
    else if (displayKey_ != key)
    {
        auto buf = std::make_shared<std::vector<float>> (w->frames);
        for (uint32_t i = 0; i < w->frames; ++i) (*buf)[i] = monoAt (*w, i);
        display_ = buf; displayKey_ = key;
        waveform_.setBuffer (display_, (double) w->sampleRate);
        seam_.setBuffer (display_, (double) w->sampleRate);
        cursorFrame_ = -1;
    }
    if (! have && displayKey_.valid() == false && waveform_.frames() != 0) { waveform_.setBuffer (nullptr, 44100.0); seam_.setBuffer (nullptr, 44100.0); }
    editorBadge_ = have ? proc_.typeBadge (key) : dash();
    editorBadgeBg_ = have ? badgeColour (proc_.typeOf (key)) : col::idleDot;
    if (have)
    {
        const juce::String slotTag = key == proc_.slotKey (kSlotA) ? juce::String::fromUTF8 ("A \xE2\x96\xB8 ") : key == proc_.slotKey (kSlotB) ? juce::String::fromUTF8 ("B \xE2\x96\xB8 ") : juce::String();
        editorTitle_ = slotTag + proc_.pathOf (key) + (e->dirty && proc_.isWavSource (key) ? juce::String::fromUTF8 (" \xE2\x97\x8F") : juce::String());
    }
    else editorTitle_ = juce::String::fromUTF8 ("\xE2\x80\x94 no sample selected \xE2\x80\x94");
    const bool latched = proc_.latched();
    playButton_.setToggleState (latched, juce::dontSendNotification);
    playButton_.setButtonText (latched ? juce::String::fromUTF8 ("\xE2\x96\xA0 STOP") : juce::String::fromUTF8 ("\xE2\x96\xB8 PLAY ") + (have ? nn (e->rootKey) : juce::String()));
    loopSeg_.setIndex (have ? e->loopKind : 0, false);
    const bool wavSrc = have && proc_.isWavSource (key);
    saveButton_.setAlpha (wavSrc ? 1.0f : 0.4f);
    saveButton_.setTooltip (have ? (wavSrc ? "write smpl + bext back into " + proc_.pathOf (key)
                                            : editorBadge_ + juce::String::fromUTF8 (" is a read-only container \xE2\x80\x94 use EXPORT SAMPLE"))
                                 : juce::String ("nothing selected"));
    saveAsButton_.setTooltip (have ? "write the current sample to a new file: <PREFIX>_<NOTE>.wav" : "nothing selected");
    if (have)
    {
        waveform_.setLoop ((juce::int64) e->loopStart, (juce::int64) e->loopEnd);
        waveform_.setLoopEnabled (e->loopKind != 2);
        seam_.setLoop ((juce::int64) e->loopStart, (juce::int64) e->loopEnd);
        const double rate = (double) w->sampleRate;
        const uint32_t len = e->loopEnd - e->loopStart + 1;
        startField_.setValue (e->loopStart);
        endField_.setValue (e->loopEnd);
        lenField_.setValue (len);
        rootField_.setShown (nn (e->rootKey));
        fineField_.setValue (e->fineCents);
        attackField_.setValue (e->attackMs);
        releaseField_.setValue (e->releaseMs);
        prefixField_.setShown (e->prefix);
        descField_.setShown (e->description);
        const double per = loopPeriods (len, rate, e->rootKey, e->fineCents);
        periods_.setText (juce::String (per, 2), ! periodsNearInteger (per));
        const double c = cursorFrame_ >= 0 ? (double) cursorFrame_ : std::max (0.0, proc_.engine().lastPlayhead (kSlotCur));
        cursorLine_ = "cursor " + juce::String ((juce::int64) c) + " smp  " + msOf (c, rate) + "   root " + nn (e->rootKey)
                    + "   loop " + juce::String ((int) len) + " smp  " + msOf (len, rate) + "  " + juce::String (per, 2) + " periods";
        assistsDirty_ = true;
    }
    else
    {
        for (auto* f : { &startField_, &endField_, &lenField_, &rootField_, &fineField_, &attackField_, &releaseField_, &prefixField_, &descField_ }) f->setShown ({});
        periods_.setText (dash(), false);
        clickMeter_.set (0.0, false);
        loudness_.setText (dash(), false);
        cursorLine_.clear();
    }
    face_.repaint (0, 472, 944, 324);
}

void ScoutEditor::refreshAssists()
{
    assistsDirty_ = false;
    const SampleKey key = cur();
    const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
    const SampleEdits* e = key.valid() ? proc_.edits (key) : nullptr;
    if (w == nullptr || e == nullptr) return;
    clickMeter_.set (clickRatio (*w, e->loopStart, e->loopEnd, (LoopKind) e->loopKind), e->loopKind != 2);
    loudness_.setText (juce::String (loopRmsDb (*w, e->loopStart, e->loopEnd), 1) + " dB", false);
}

void ScoutEditor::rebuildExport()
{
    ExportColumn::Info info;
    const ExportSettings& es = proc_.exportSettings();
    const SampleKey key = cur();
    const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
    const SampleEdits* e = key.valid() ? proc_.edits (key) : nullptr;
    info.folder = es.folder; info.quick = es.quick; info.convert16 = es.convert16; info.fold = es.fold;
    info.foldEnabled = (w != nullptr && w->isStereo()) || es.convert16;
    info.fmtNote = proc_.exportFormatNote (key);
    info.sampleName = w != nullptr ? proc_.exportFileName (key) : juce::String();
    info.rangeStart = es.rangeStart; info.rangeEnd = es.rangeEnd;
    const juce::int64 rangeLen = (juce::int64) es.rangeEnd - (juce::int64) es.rangeStart;
    const bool out = e != nullptr && (e->loopStart < es.rangeStart || e->loopEnd >= es.rangeEnd);
    info.rangeNote = rangeLen > 0 ? juce::String (rangeLen) + " smp" + kMiddot + (out ? "markers outside range" : "markers remapped") : "end must exceed start";
    info.rangeWarn = rangeLen <= 0 || out;
    info.scope = es.scope; info.scopeLabel = proc_.batchScopeLabel(); info.skip = es.skipExisting;
    info.batch = proc_.batch();
    const int metaSrc = key.valid() ? key.src : proc_.selectedSource();
    if (const Source* s = proc_.source (metaSrc))
    {
        info.metaTitle = s->type == SourceType::Sf2 ? "SF2 INFO" : s->type == SourceType::Module ? "MODULE INFO" : s->type == SourceType::Wav ? "WAV CHUNKS" : "LOAD ERROR";
        info.metaBadge = proc_.typeBadge (*s);
        info.metaBadgeBg = badgeColour (s->type);
    }
    info.metaRows = proc_.metadata (metaSrc);
    exportCol_.update (info);
}

// ------------------------------------------------------------------ live polling
void ScoutEditor::timerCallback()
{
    if (proc_.sourceGeneration() != seenGeneration_) rebuildAll();
    refreshLive();
    if (assistsDirty_ && juce::Time::getMillisecondCounter() - assistsAt_ > 120)
    {
        assistsAt_ = juce::Time::getMillisecondCounter();
        refreshAssists();
    }
    if (isStandalone() && ++midiPollTicks_ >= 30) { midiPollTicks_ = 0; refreshMidiDevices (false); }
}

void ScoutEditor::refreshLive()
{
    const ScoutEngine& en = proc_.engine();
    bool readouts = false;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const uint32_t seq = en.noteCounter (slot);
        if (seq != seenNoteSeq_[slot])
        {
            seenNoteSeq_[slot] = seq;
            if (slot != kSlotCur) readouts = true;
            if (slot == sfSlot_ || (slot == kSlotCur && proc_.typeOf (cur()) == SourceType::Sf2)) { lastSf2Note_ = en.lastNote (slot); rebuildZoneMap(); face_.repaint (0, 368, 944, 104); }
        }
    }
    if (readouts) rebuildReadouts();
    const double ph = en.lastPlayhead (kSlotCur);
    if (ph != shownPlayhead_)
    {
        shownPlayhead_ = ph;
        waveform_.setPlayhead (ph);
        seam_.setPlayhead (ph);
        if (cursorFrame_ < 0 && ph >= 0.0) rebuildEditor();
    }
    if (proc_.latched() && ph < 0.0 && en.activeVoiceCount() == 0 && juce::Time::getMillisecondCounter() - latchedAt_ > 300)
    {
        // a one-shot ran out: unlatch so the button reads PLAY again
        proc_.setLatched (false);
    }
    const int n = en.activeVoiceCount();
    if (n != shownVoices_) { shownVoices_ = n; face_.repaint (0, 796, kW, 44); }
    if (proc_.batch().running) rebuildExport();
}

// ------------------------------------------------------------------ actions
void ScoutEditor::setStatus (const juce::String& msg, bool warn)
{
    status_ = msg; statusWarn_ = warn;
    face_.repaint (0, 472, 944, 30);
}

void ScoutEditor::chooseSource()
{
    juce::String filter = "*.sf2;*.SF2;*.wav;*.WAV";
    for (const auto& e : ModuleSource::supportedExtensions()) filter << ";*." << juce::String (e);
    chooser_ = std::make_unique<juce::FileChooser> ("Open a SoundFont, tracker module or WAV", juce::File(), filter);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            for (const auto& f : fc.getResults()) if (f.existsAsFile()) loadFile (f);
        });
}

void ScoutEditor::loadFile (const juce::File& f)
{
    const juce::String err = proc_.openSource (f);
    if (err.isNotEmpty()) { setStatus ("ERROR: " + err, true); return; }
    const Source* s = proc_.source (proc_.selectedSource());
    if (s != nullptr)
        setStatus ("loaded " + s->fileName + kMiddot + (s->type == SourceType::Sf2 ? juce::String (s->sampleCount()) + " presets" : s->type == SourceType::Module ? juce::String (s->sampleCount()) + " samples" : juce::String ("1 sample")), false);
    grabKeyboardFocus();
}

void ScoutEditor::unloadSelected()
{
    const int id = proc_.selectedSource();
    const Source* s = proc_.source (id);
    if (s == nullptr) { setStatus ("nothing selected", true); return; }
    bool dirtyWav = false;
    if (s->type == SourceType::Wav)
        if (const SampleEdits* e = proc_.edits ({ id, -1, -1 })) dirtyWav = e->dirty;
    const juce::String name = s->fileName;
    if (dirtyWav)
    {
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                        .withTitle ("Unsaved loop markers")
                        .withMessage (name + " has marker edits that are not saved.")
                        .withButton ("Save").withButton ("Discard").withButton ("Cancel")
                        .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [this, id] (int result)
        {
            if (result == 1) { const juce::String e = proc_.saveWav ({ id, -1, -1 }); if (e.isNotEmpty()) { setStatus ("ERROR: " + e, true); return; } }
            else if (result != 2) return;
            proc_.unloadSource (id);
        });
        return;
    }
    proc_.unloadSource (id);
    setStatus ("unloaded " + name, false);
}

void ScoutEditor::assign (int slot)
{
    const SampleKey key = cur();
    if (! key.valid()) { setStatus ("select a sample first", true); return; }
    const juce::String err = proc_.assignSlot (slot, key);
    if (err.isNotEmpty()) { setStatus ("ERROR: " + err, true); return; }
    setStatus (proc_.nameOf (key) + juce::String::fromUTF8 (" \xE2\x86\x92 ") + (slot == kSlotA ? "A" : "B"), false);
}

void ScoutEditor::step (int delta)
{
    const juce::String err = proc_.stepSample (delta);
    if (err.isNotEmpty()) setStatus (err, true);
}

void ScoutEditor::applyEdit (const std::function<void (SampleEdits&)>& fn)
{
    const SampleKey key = cur();
    if (! key.valid()) return;
    SampleEdits e = proc_.editsOrDefault (key);
    fn (e);
    proc_.setEdits (key, e);
}

void ScoutEditor::nudge (bool endMarker, juce::int64 delta)
{
    applyEdit ([endMarker, delta] (SampleEdits& e)
    {
        if (endMarker) e.loopEnd   = (uint32_t) juce::jmax<juce::int64> ((juce::int64) e.loopStart, (juce::int64) e.loopEnd + delta);
        else           e.loopStart = (uint32_t) juce::jlimit<juce::int64> (0, (juce::int64) e.loopEnd, (juce::int64) e.loopStart + delta);
    });
}

void ScoutEditor::setLoopKind (int kind)
{
    applyEdit ([kind] (SampleEdits& e) { e.loopKind = kind; e.loopOverride = true; });
}

void ScoutEditor::doSave()
{
    const SampleKey key = cur();
    if (! key.valid()) { setStatus ("nothing selected", true); return; }
    const juce::String err = proc_.saveWav (key);
    if (err.isNotEmpty()) { setStatus (err, true); return; }
    const SampleEdits e = proc_.editsOrDefault (key);
    setStatus ("saved " + proc_.nameOf (key) + ".wav" + kMiddot + "smpl " + juce::String ((int) e.loopStart) + enDash() + juce::String ((int) e.loopEnd) + " " + kindShort (e.loopKind) + kMiddot + "bext updated", false);
}

void ScoutEditor::doSaveAs()
{
    const SampleKey key = cur();
    if (! key.valid()) { setStatus ("nothing selected", true); return; }
    chooser_ = std::make_unique<juce::FileChooser> ("Save as", proc_.saveAsSuggestion (key), "*.wav");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, key] (const juce::FileChooser& fc)
        {
            juce::File f = fc.getResult();
            if (f.getFullPathName().isEmpty()) return;
            if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
            const juce::String err = proc_.saveAs (key, f);
            if (err.isNotEmpty()) setStatus ("ERROR: " + err, true);
            else setStatus ("saved as " + f.getFileName() + juce::String::fromUTF8 (" \xE2\x86\x92 ") + f.getParentDirectory().getFullPathName(), false);
        });
}

void ScoutEditor::doSuggest()
{
    const SampleKey key = cur();
    const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
    if (w == nullptr) { setStatus ("nothing selected", true); return; }
    const SampleEdits e = proc_.editsOrDefault (key);
    const SpliceSuggestion s = suggestSplice (*w, e.loopStart, e.loopEnd);
    if (! s.found) { setStatus ("SUGGEST: no better splice within " + juce::String::fromUTF8 ("\xC2\xB1") + "200 ms", true); return; }
    const uint32_t before = e.loopEnd;
    applyEdit ([s] (SampleEdits& ed) { ed.loopEnd = s.loopEnd; });
    setStatus ("SUGGEST: END " + juce::String ((int) before) + juce::String::fromUTF8 (" \xE2\x86\x92 ") + juce::String ((int) s.loopEnd) + " (score " + juce::String (s.score, 3) + ")", false);
}

void ScoutEditor::doAutoRoot()
{
    const SampleKey key = cur();
    const WavSample* w = key.valid() ? proc_.decoded (key) : nullptr;
    if (w == nullptr) { setStatus ("nothing selected", true); return; }
    const SampleEdits e = proc_.editsOrDefault (key);
    const RootEstimate r = autoDetectRoot (*w, e.loopStart, e.loopEnd);
    if (! r.found) { setStatus ("AUTO-DETECT: no stable pitch in the loop region", true); return; }
    applyEdit ([r] (SampleEdits& ed) { ed.rootKey = r.note; ed.fineCents = std::round (r.cents); });
    setStatus ("AUTO-DETECT: " + juce::String (r.hz, 1) + " Hz" + juce::String::fromUTF8 (" \xE2\x86\x92 ") + nn (r.note) + juce::String::formatted (" %+dc", (int) std::lround (r.cents)), false);
}

void ScoutEditor::togglePlay()
{
    if (! cur().valid()) { setStatus ("nothing selected", true); return; }
    latchedAt_ = juce::Time::getMillisecondCounter();
    proc_.setLatched (! proc_.latched());
}

void ScoutEditor::zoneKey (int key, bool down)
{
    if (sfSlot_ < 0) return;
    const int zi = zoneStrip_.zoneOf (key);
    if (zi < 0) return;
    const SampleKey slotKey = proc_.slotKey (sfSlot_);
    const SampleKey zoneKey { slotKey.src, slotKey.a, zi };
    if (down)
    {
        if (cur() != zoneKey) { const juce::String err = proc_.selectSample (zoneKey); if (err.isNotEmpty()) { setStatus ("ERROR: " + err, true); return; } }
        proc_.auditionSlot (kSlotCur, key, true);
        lastSf2Note_ = key;
        rebuildZoneMap();
        face_.repaint (0, 368, 944, 104);
    }
    else proc_.auditionSlot (kSlotCur, key, false);
}

void ScoutEditor::browseFolder()
{
    const juce::String start = proc_.exportSettings().folder;
    chooser_ = std::make_unique<juce::FileChooser> ("Export folder", start.isNotEmpty() ? juce::File (start) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const juce::File d = fc.getResult();
            if (! d.isDirectory()) return;
            ExportSettings e = proc_.exportSettings();
            e.folder = d.getFullPathName();
            if (e.quick == 1) e.referenceFolder = e.folder;
            else if (e.quick == 2) e.romFolder = e.folder;
            proc_.setExportSettings (e);
            setStatus ("export folder: " + e.folder, false);
        });
}

void ScoutEditor::quickTarget (int which)
{
    ExportSettings e = proc_.exportSettings();
    e.quick = which;
    const juce::String remembered = which == 1 ? e.referenceFolder : e.romFolder;
    if (remembered.isNotEmpty()) { e.folder = remembered; proc_.setExportSettings (e); setStatus ("export folder: " + e.folder, false); }
    else { proc_.setExportSettings (e); browseFolder(); }
}

void ScoutEditor::exportSample()
{
    const SampleKey key = cur();
    if (! key.valid()) { setStatus ("nothing selected", true); return; }
    juce::String path;
    const juce::String err = proc_.exportSample (key, path);
    if (err.isNotEmpty()) { setStatus ("ERROR: " + err, true); return; }
    const SampleEdits e = proc_.editsOrDefault (key);
    setStatus ("exported " + proc_.nameOf (key) + juce::String::fromUTF8 (" \xE2\x86\x92 ") + juce::File (path).getFileName() + " (smpl " + juce::String ((int) e.loopStart) + enDash() + juce::String ((int) e.loopEnd) + (e.loopKind == 1 ? " type 1" : " type 0") + ", bext)", false);
}

void ScoutEditor::exportRange()
{
    const SampleKey key = cur();
    if (! key.valid()) { setStatus ("nothing selected", true); return; }
    juce::String path; bool dropped = false;
    const juce::String err = proc_.exportRange (key, path, dropped);
    if (err.isNotEmpty()) { setStatus ("EXPORT RANGE: " + err, true); return; }
    const ExportSettings& es = proc_.exportSettings();
    setStatus ("EXPORT RANGE [" + juce::String ((int) es.rangeStart) + ", " + juce::String ((int) es.rangeEnd) + ") " + juce::String::fromUTF8 ("\xE2\x86\x92 ") + juce::String ((int) (es.rangeEnd - es.rangeStart)) + " samples written"
               + (dropped ? juce::String::fromUTF8 (" \xC2\xB7 WARNING markers dropped") : ", markers remapped"), dropped);
}

void ScoutEditor::batchToggle()
{
    if (proc_.batch().running) { proc_.cancelBatch(); setStatus ("batch cancelled", true); return; }
    const juce::String err = proc_.startBatch();
    if (err.isNotEmpty()) { setStatus ("BATCH: " + err, true); return; }
    setStatus ("batch: " + juce::String (proc_.batch().total) + " files " + juce::String::fromUTF8 ("\xE2\x86\x92 ") + proc_.exportSettings().folder, false);
}

// ------------------------------------------------------------------ keyboard
bool ScoutEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { doSave(); return true; }
    if (k == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { doSaveAs(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
    if (k == juce::KeyPress::tabKey) { proc_.swapToggle(); return true; }
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    const juce::juce_wchar c = k.getTextCharacter();
    switch (c)
    {
        case 'f': case 'F': setLoopKind (0); return true;
        case 'p': case 'P': setLoopKind (1); return true;
        case 'o': case 'O': setLoopKind (2); return true;
        case 'l': case 'L': if (modeAttachment_) modeAttachment_->setValueAsCompleteGesture (modeSeg_.index() == 0 ? 1.0f : 0.0f); return true;
        case '[': nudge (false, -1);   return true;
        case ']': nudge (false, +1);   return true;
        case '{': nudge (false, -100); return true;
        case '}': nudge (false, +100); return true;
        case ';': nudge (true, -1);    return true;
        case '\'': nudge (true, +1);   return true;
        case ':': nudge (true, -100);  return true;
        case '"': nudge (true, +100);  return true;
        case ',': step (-1); return true;
        case '.': step (+1); return true;
        default: break;
    }
    // QWERTY piano: z s x d c v g b h n j m = C4 .. B4, routed like MIDI
    static const char keys[12] = { 'z', 's', 'x', 'd', 'c', 'v', 'g', 'b', 'h', 'n', 'j', 'm' };
    const juce::juce_wchar lc = juce::CharacterFunctions::toLowerCase (c);
    for (int i = 0; i < 12; ++i)
        if (lc == (juce::juce_wchar) keys[i])
        {
            if (! qwertyDown_[i]) { qwertyDown_[i] = true; proc_.auditionKeyboard (kQwertyBase + i, true); }
            return true;
        }
    return false;
}

bool ScoutEditor::keyStateChanged (bool)
{
    static const char keys[12] = { 'z', 's', 'x', 'd', 'c', 'v', 'g', 'b', 'h', 'n', 'j', 'm' };
    bool handled = false;
    for (int i = 0; i < 12; ++i)
        if (qwertyDown_[i] && ! juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keys[i])))
        {
            qwertyDown_[i] = false;
            proc_.auditionKeyboard (kQwertyBase + i, false);
            handled = true;
        }
    return handled;
}

// ------------------------------------------------------------------ drag & drop
bool ScoutEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (ScoutProcessor::isSourceName (f)) return true;
    return false;
}

void ScoutEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files) if (ScoutProcessor::isSourceName (f)) loadFile (juce::File (f));
}

// ------------------------------------------------------------------ standalone MIDI device
void ScoutEditor::refreshMidiDevices (bool force)
{
    if (! isStandalone()) return;
    auto devices = juce::MidiInput::getAvailableDevices();
    juce::StringArray ids { "" };
    juce::StringArray names { "ALL MIDI INPUTS" };
    for (const auto& d : devices) { ids.add (d.identifier); names.add (d.name); }
    if (! force && ids == midiDeviceIds_) return;
    midiDeviceIds_ = ids;
    midiDeviceCombo_.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) midiDeviceCombo_.addItem (names[i], i + 1);
    int row = 0;
    for (int i = 1; i < ids.size(); ++i) if (ids[i] == proc_.midiInputDevice()) row = i;
    if (row == 0 && proc_.midiInputDevice().isNotEmpty()) proc_.setMidiInputDevice ({});   // vanished: back to ALL
    midiDeviceCombo_.setSelectedItemIndex (row, juce::dontSendNotification);
    if (! force) applyMidiDevice();
}

void ScoutEditor::applyMidiDevice()
{
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        auto& dm = holder->deviceManager;
        const juce::String want = proc_.midiInputDevice();
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            dm.setMidiInputDeviceEnabled (d.identifier, want.isEmpty() || d.identifier == want);
    }
#endif
}

} // namespace sf2scout
