// EXPORT column (README 5): folder + quick targets, audio / stereo options,
// EXPORT SAMPLE, EXPORT RANGE, BATCH with progress + log, METADATA box.
// Bounds are the manifest's, relative to the column origin (945, 48).
#pragma once
#include "Widgets.h"
#include "Readout.h"
#include "../PluginProcessor.h"

namespace sf2scout::ui
{

// METADATA rows: key 44 wide, value wrapped (pre-wrap), mono 10, line-height 14
class MetaBox : public juce::Component
{
public:
    void setRows (const std::vector<ScoutProcessor::MetaRow>& rows, int width)
    {
        rows_.clear();
        int y = 8;
        for (const auto& r : rows)
        {
            Line l; l.k = r.k;
            juce::AttributedString as;
            as.setFont (mono (10.0f));
            as.setColour (col::text);
            as.setLineSpacing (4.0f);
            as.append (r.v);
            l.layout.createLayout (as, (float) (width - 16 - 44));
            l.y = y; l.h = std::max (14, (int) std::ceil (l.layout.getHeight()));
            y += l.h + 3;
            rows_.push_back (std::move (l));
        }
        height_ = y + 5;
        setSize (width, std::max (height_, getParentHeight()));
        repaint();
    }
    int contentHeight() const { return height_; }
    void paint (juce::Graphics& g) override
    {
        for (const auto& l : rows_)
        {
            g.setColour (col::label); g.setFont (mono (10.0f));
            g.drawText (l.k, 8, l.y, 44, 14, juce::Justification::centredLeft, false);
            l.layout.draw (g, juce::Rectangle<float> (52.0f, (float) l.y, (float) (getWidth() - 60), (float) l.h));
        }
    }
private:
    struct Line { juce::String k; juce::TextLayout layout; int y = 0, h = 14; };
    std::vector<Line> rows_;
    int height_ = 0;
};

class ExportColumn : public juce::Component
{
public:
    struct Info
    {
        juce::String folder;
        int quick = 0;
        bool convert16 = false;
        int fold = 0;
        bool foldEnabled = true;
        juce::String fmtNote, sampleName, rangeNote;
        bool rangeWarn = false;
        uint32_t rangeStart = 0, rangeEnd = 0;
        int scope = 0;
        juce::String scopeLabel = "MODULE";
        bool skip = true;
        BatchState batch;
        juce::String metaTitle = "METADATA", metaBadge = dash();
        juce::Colour metaBadgeBg = col::idleDot;
        std::vector<ScoutProcessor::MetaRow> metaRows;
    };

    std::function<void()> onBrowse, onExportSample, onExportRange, onBatch;
    std::function<void (int)> onQuick, onFmt, onFold, onScope;
    std::function<void (bool)> onSkip;
    std::function<void (uint32_t, uint32_t)> onRange;

    ExportColumn()
    {
        addAndMakeVisible (browse_);   browse_.onClick = [this] { if (onBrowse) onBrowse(); };
        addAndMakeVisible (quickRef_); quickRef_.onClick = [this] { if (onQuick) onQuick (1); };
        addAndMakeVisible (quickRom_); quickRom_.onClick = [this] { if (onQuick) onQuick (2); };
        addAndMakeVisible (audioSeg_); audioSeg_.onChange = [this] (int i) { if (onFmt) onFmt (i); };
        addAndMakeVisible (stereoSeg_); stereoSeg_.onChange = [this] (int i) { if (onFold) onFold (i); };
        addAndMakeVisible (exportSample_); exportSample_.onClick = [this] { if (onExportSample) onExportSample(); };
        addAndMakeVisible (rangeStart_); addAndMakeVisible (rangeEnd_);
        rangeStart_.onCommit = [this] (const juce::String& t) { if (onRange) onRange ((uint32_t) juce::jmax (0, t.getIntValue()), info_.rangeEnd); };
        rangeEnd_.onCommit   = [this] (const juce::String& t) { if (onRange) onRange (info_.rangeStart, (uint32_t) juce::jmax (0, t.getIntValue())); };
        addAndMakeVisible (exportRange_); exportRange_.onClick = [this] { if (onExportRange) onExportRange(); };
        addAndMakeVisible (batchSeg_); batchSeg_.onChange = [this] (int i) { if (onScope) onScope (i); };
        addAndMakeVisible (skip_); skip_.onChange = [this] (bool on) { if (onSkip) onSkip (on); };
        addAndMakeVisible (batch_); batch_.onClick = [this] { if (onBatch) onBatch(); };
        addAndMakeVisible (progress_);
        metaView_.setViewedComponent (&meta_, false);
        metaView_.setScrollBarsShown (true, false);
        metaView_.setScrollBarThickness (8);
        addAndMakeVisible (metaView_);
        browse_.setTooltip ("choose the export folder (remembered)");
        exportSample_.setTooltip ("write the selected sample as a loop-tagged WAV (smpl + bext) into the folder");
        exportRange_.setTooltip ("write exactly [start, end) frames; markers remapped, or dropped with a warning");
    }

    void update (const Info& info)
    {
        info_ = info;
        quickRef_.setToggleState (info.quick == 1, juce::dontSendNotification);
        quickRom_.setToggleState (info.quick == 2, juce::dontSendNotification);
        audioSeg_.setIndex (info.convert16 ? 1 : 0, false);
        stereoSeg_.setIndex (info.fold, false);
        stereoSeg_.setAlpha (info.foldEnabled ? 1.0f : 0.45f);
        rangeStart_.setValue ((double) info.rangeStart);
        rangeEnd_.setValue ((double) info.rangeEnd);
        batchSeg_.setLabel (0, info.scopeLabel);
        batchSeg_.setIndex (info.scope, false);
        skip_.setToggled (info.skip, false);
        const bool running = info.batch.running;
        batch_.setButtonText (running ? "CANCEL" : (info.batch.lastComplete ? "EXPORT ALL AGAIN" : "EXPORT ALL"));
        if (running) batch_.setColours (col::inset, col::badgeErr, col::hoverBtn, true);
        else         batch_.setColours (col::accent, juce::Colours::white, col::accentHov, false);
        progress_.setFraction (info.batch.total > 0 ? (double) info.batch.done / (double) info.batch.total : 0.0);
        meta_.setRows (info.metaRows, 231);
        repaint();
    }

    void resized() override
    {
        browse_.setBounds (217, 48, 26, 24);
        quickRef_.setBounds (12, 76, 113, 20);
        quickRom_.setBounds (130, 76, 113, 20);
        audioSeg_.setBounds (74, 106, 169, 22);
        stereoSeg_.setBounds (74, 134, 169, 22);
        exportSample_.setBounds (12, 195, 231, 28);
        rangeStart_.setBounds (12, 264, 112, 22);
        rangeEnd_.setBounds (131, 264, 112, 22);
        exportRange_.setBounds (12, 292, 104, 24);
        batchSeg_.setBounds (74, 337, 169, 22);
        skip_.setBounds (74, 365, 169, 15);
        batch_.setBounds (12, 386, 231, 28);
        progress_.setBounds (12, 424, 231, 12);
        metaView_.setBounds (13, 587, 229, 148);
        meta_.setRows (info_.metaRows, 231);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::window); g.fillRect (getLocalBounds());
        g.setColour (col::borderSec); g.fillRect (0, 0, 1, getHeight());       // divider left of the column
        drawSmallLabel (g, "EXPORT", { 12, 10, 120, 14 });
        g.setFont (mono (10.0f)); g.setColour (col::text3);
        g.drawText ("smpl + bext", 12, 10, 231, 14, juce::Justification::centredRight, false);
        drawSmallLabel (g, "FOLDER", { 12, 34, 120, 11 });
        // folder field: path mono 11, rtl-ellipsised
        juce::Rectangle<int> ff (12, 48, 201, 24);
        g.setColour (col::inset); g.fillRect (ff);
        g.setColour (col::control); g.drawRect (ff, 1);
        g.setFont (mono (11.0f)); g.setColour (info_.folder.isEmpty() ? col::text3 : col::text);
        g.drawText (tailFit (info_.folder.isEmpty() ? juce::String ("choose a folder") : info_.folder, mono (11.0f), ff.getWidth() - 12), ff.reduced (6, 0), juce::Justification::centredLeft, false);
        drawSmallLabel (g, "AUDIO", { 12, 106, 56, 22 });
        drawSmallLabel (g, "STEREO", { 12, 134, 56, 22 });
        g.setFont (mono (10.0f)); g.setColour (col::text3);
        g.drawText (info_.fmtNote, 74, 162, 169, 12, juce::Justification::centredLeft, true);
        g.setColour (col::hairline); g.fillRect (12, 184, 231, 1);
        g.setFont (mono (10.0f)); g.setColour (col::text2);
        g.drawText (juce::String::fromUTF8 ("\xE2\x86\x92 ") + (info_.sampleName.isEmpty() ? dash() : info_.sampleName), 12, 229, 231, 12, juce::Justification::centredLeft, true);
        drawSmallLabel (g, "RANGE START", { 12, 251, 112, 11 });
        drawSmallLabel (g, "END (EXCL)", { 131, 251, 112, 11 });
        g.setFont (mono (10.0f)); g.setColour (info_.rangeWarn ? col::warn : col::text3);
        g.drawText (info_.rangeNote, 122, 292, 121, 24, juce::Justification::centredLeft, true);
        g.setColour (col::hairline); g.fillRect (12, 326, 231, 1);
        drawSmallLabel (g, "BATCH", { 12, 337, 56, 22 });
        g.setFont (mono (11.0f)); g.setColour (col::text2);
        const juce::String count = juce::String (info_.batch.done) + " / " + juce::String (info_.batch.total);
        const int cw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (11.0f, true), count)) + 4;
        g.drawText (info_.batch.running ? info_.batch.current : (info_.batch.current.isEmpty() ? juce::String ("idle") : info_.batch.current), 12, 442, 231 - cw, 14, juce::Justification::centredLeft, true);
        g.setFont (mono (11.0f, true)); g.setColour (col::text);
        g.drawText (count, 12, 442, 231, 14, juce::Justification::centredRight, false);
        g.setFont (mono (10.0f));
        for (int i = 0; i < juce::jmin (4, info_.batch.log.size()); ++i)
        {
            const juce::String& line = info_.batch.log[i];
            g.setColour (line.startsWith ("done") ? col::okDot : (line.startsWith ("cancelled") || line.startsWith ("FAILED")) ? col::warn : col::text3);
            g.drawText (line, 12, 464 + i * 13, 231, 13, juce::Justification::centredLeft, true);
        }
        g.setColour (col::hairline); g.fillRect (12, 558, 231, 1);
        drawSmallLabel (g, info_.metaTitle, { 12, 568, 160, 14 });
        drawBadge (g, 243 - badgeWidth (info_.metaBadge), 568, info_.metaBadge, info_.metaBadgeBg, 14);
        juce::Rectangle<int> box (12, 586, 231, 150);
        g.setColour (col::inset); g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (col::hairline); g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

private:
    // keeps the END of a path visible: drops leading characters behind an ellipsis until it fits
    static juce::String tailFit (const juce::String& s, const juce::Font& f, int width)
    {
        if (juce::GlyphArrangement::getStringWidth (f, s) <= (float) width) return s;
        const juce::String ell = juce::String::fromUTF8 ("\xE2\x80\xA6");
        for (int cut = 1; cut < s.length(); ++cut)
        {
            const juce::String t = ell + s.substring (cut);
            if (juce::GlyphArrangement::getStringWidth (f, t) <= (float) width) return t;
        }
        return ell;
    }

    Info info_;
    FlatButton browse_      { juce::String::fromUTF8 ("\xE2\x80\xA6"), col::inset, col::text, col::hoverBtn, 3.0f, mono (11.0f, true), true };
    FlatButton quickRef_    { "reference", col::inset, col::text2, col::hoverBtn, 3.0f, mono (10.0f, true), true };
    FlatButton quickRom_    { "rom", col::inset, col::text2, col::hoverBtn, 3.0f, mono (10.0f, true), true };
    SegmentSwitch audioSeg_ { { "NATIVE", "16b/44.1 MONO" }, { 0, 0 }, 10.0f };
    SegmentSwitch stereoSeg_{ { "SUM", "L ONLY" }, { 0, 0 }, 10.0f };
    FlatButton exportSample_{ "EXPORT SAMPLE", col::accent, juce::Colours::white, col::accentHov, 4.0f, buttonFont() };
    NumField rangeStart_, rangeEnd_;
    FlatButton exportRange_ { "EXPORT RANGE", col::inset, col::text, col::hoverBtn, 4.0f, buttonFont(), true };
    SegmentSwitch batchSeg_ { { "MODULE", "SOURCE" }, { 0, 0 }, 10.0f };
    Checkbox skip_          { "skip existing files", 15, 11.0f };
    FlatButton batch_       { "EXPORT ALL", col::accent, juce::Colours::white, col::accentHov, 4.0f, buttonFont() };
    ProgressBarView progress_;
    juce::Viewport metaView_;
    MetaBox meta_;
};

}
