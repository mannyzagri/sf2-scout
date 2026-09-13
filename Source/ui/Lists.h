// SOURCE LIST (tree) and CONTENTS (sample table) -- README 2.1 / 2.2. Both are
// stock ListBoxes fed value rows the editor builds from the processor; no
// pointers into a source ever live here.
#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sf2scout::ui
{

// ----------------------------------------------------------------- source list
struct TreeRow
{
    int srcId = -1;
    int preset = -1;                    // >= 0: a preset row under an SF2 source
    int pad = 8;                        // 8 for sources, 26 for presets
    juce::String caret, badge, label, sub;
    juce::Colour badgeBg = col::text3, fg = col::text;
    bool medium = false, selected = false, isError = false;
};

class SourceListView : public juce::Component,
                       private juce::ListBoxModel
{
public:
    SourceListView()
    {
        list_.setModel (this);
        list_.setRowHeight (32);
        list_.setColour (juce::ListBox::backgroundColourId, col::window);
        list_.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list_.getViewport()->setScrollBarsShown (true, false);
        list_.getViewport()->setScrollBarThickness (8);
        addAndMakeVisible (list_);
    }
    std::function<void (const TreeRow&)> onRowClicked;
    void setRows (std::vector<TreeRow> rows) { rows_ = std::move (rows); list_.updateContent(); list_.repaint(); }
    void resized() override { list_.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return (int) rows_.size() + 1; }   // + the drop hint row
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row == (int) rows_.size())
        {
            g.setColour (col::text3); g.setFont (sans (11.0f));
            g.drawText (juce::String::fromUTF8 ("drop .sf2 \xC2\xB7 .it .xm .mod .s3m \xC2\xB7 .wav here"), juce::Rectangle<int> (12, 0, w - 24, h), juce::Justification::centredLeft, true);
            return;
        }
        if (row < 0 || row >= (int) rows_.size()) return;
        const TreeRow& r = rows_[(size_t) row];
        if (r.selected) { g.setColour (col::selRow); g.fillRect (0, 0, w, h); }
        g.setColour (col::hairlineRow); g.fillRect (0, h - 1, w, 1);
        int x = r.pad;
        g.setColour (col::text3); g.setFont (mono (11.0f));
        g.drawText (r.caret, x, 0, 10, h, juce::Justification::centred, false);
        x += 10 + 7;
        if (r.badge.isNotEmpty()) x += drawBadge (g, x, (h - 15) / 2, r.badge, r.badgeBg) + 7;
        const int subW = r.sub.isEmpty() ? 0 : (int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (10.0f), r.sub)) + 7;
        g.setColour (r.fg); g.setFont (nameFont (r.medium));
        g.drawText (r.label, x, 0, w - x - 8 - subW, h, juce::Justification::centredLeft, true);
        g.setColour (col::text3); g.setFont (mono (10.0f));
        g.drawText (r.sub, w - 8 - subW + 7, 0, subW - 7, h, juce::Justification::centredRight, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) rows_.size() && onRowClicked) onRowClicked (rows_[(size_t) row]);
    }
    juce::ListBox list_;
    std::vector<TreeRow> rows_;
};

// ----------------------------------------------------------------- contents list
struct ListRow
{
    SampleKey key;
    juce::String idx, name, fmt, frames, kind, rate;
    juce::Colour kindColour = col::text2;
    bool selected = false, inA = false, inB = false;
};

class ContentsListView : public juce::Component,
                         private juce::ListBoxModel
{
public:
    ContentsListView()
    {
        list_.setModel (this);
        list_.setRowHeight (26);
        list_.setColour (juce::ListBox::backgroundColourId, col::window);
        list_.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list_.getViewport()->setScrollBarsShown (true, false);
        list_.getViewport()->setScrollBarThickness (8);
        addAndMakeVisible (list_);
    }
    std::function<void (const SampleKey&)> onRowClicked, onRowDoubleClicked;
    void setRows (std::vector<ListRow> rows, const juce::String& emptyMessage)
    {
        rows_ = std::move (rows); empty_ = emptyMessage;
        list_.setVisible (! rows_.empty());          // an empty list shows the message painted underneath
        list_.updateContent(); list_.repaint(); repaint();
    }
    void scrollToKey (const SampleKey& key)
    {
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].key == key) { list_.scrollToEnsureRowIsOnscreen ((int) i); return; }
    }
    void resized() override { list_.setBounds (getLocalBounds().withTrimmedTop (18)); }
    void paint (juce::Graphics& g) override
    {
        // column header row 18 high (listHeader): # 18 · NAME flex · FMT 40 · FRAMES 36 · LP 22 · HZ 36, gap 6, padding 0 12
        auto hr = getLocalBounds().removeFromTop (18);
        g.setColour (col::hairlineRow); g.fillRect (hr.removeFromBottom (1));
        g.setColour (col::text3); g.setFont (listHeader());
        auto r = juce::Rectangle<int> (12, 0, getWidth() - 24, 17);
        auto rate = r.removeFromRight (36); r.removeFromRight (6);
        auto lp = r.removeFromRight (22);   r.removeFromRight (6);
        auto fr = r.removeFromRight (36);   r.removeFromRight (6);
        auto fmt = r.removeFromRight (40);  r.removeFromRight (6);
        auto idx = r.removeFromLeft (18);   r.removeFromLeft (6);
        g.drawText ("#", idx, juce::Justification::centredLeft, false);
        g.drawText ("NAME", r, juce::Justification::centredLeft, false);
        g.drawText ("FMT", fmt, juce::Justification::centredLeft, false);
        g.drawText ("FRAMES", fr, juce::Justification::centredRight, false);
        g.drawText ("LP", lp, juce::Justification::centredRight, false);
        g.drawText ("HZ", rate, juce::Justification::centredRight, false);
        if (rows_.empty())
        {
            g.setColour (col::text3); g.setFont (sans (11.0f));
            g.drawFittedText (empty_, juce::Rectangle<int> (12, 18 + 14, getWidth() - 24, 60), juce::Justification::topLeft, 4);
        }
    }

private:
    int getNumRows() override { return (int) rows_.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows_.size()) return;
        const ListRow& s = rows_[(size_t) row];
        if (s.selected) { g.setColour (col::selRow); g.fillRect (0, 0, w, h); }
        else if (hover_ == row) { g.setColour (col::hoverBtn); g.fillRect (0, 0, w, h); }
        g.setColour (col::hairlineRow); g.fillRect (0, h - 1, w, 1);
        auto r = juce::Rectangle<int> (12, 0, w - 24, h - 1);
        auto rate = r.removeFromRight (36); r.removeFromRight (6);
        auto lp = r.removeFromRight (22);   r.removeFromRight (6);
        auto fr = r.removeFromRight (36);   r.removeFromRight (6);
        auto fmt = r.removeFromRight (40);  r.removeFromRight (6);
        auto idx = r.removeFromLeft (18);   r.removeFromLeft (6);
        g.setFont (mono (11.0f)); g.setColour (s.selected ? col::accent : col::text3);
        g.drawText (s.idx, idx, juce::Justification::centredLeft, false);
        // name + A / B tags
        int tagW = 0;
        if (s.inA) tagW += badgeWidth ("A") - 2 + 5;
        if (s.inB) tagW += badgeWidth ("B") - 2 + 5;
        g.setFont (nameFont()); g.setColour (col::text);
        const int nameW = std::min (r.getWidth() - tagW, (int) std::ceil (juce::GlyphArrangement::getStringWidth (nameFont(), s.name)) + 2);
        g.drawText (s.name, r.withWidth (std::max (0, nameW)), juce::Justification::centredLeft, true);
        int tx = r.getX() + nameW + 5;
        auto tag = [&] (const char* t, juce::Colour c) { const int tw = badgeWidth (t) - 2; g.setColour (c); g.fillRoundedRectangle ((float) tx, (float) (h / 2 - 7), (float) tw, 13.0f, 2.0f); g.setColour (juce::Colours::white); g.setFont (badgeFont()); g.drawText (t, tx, h / 2 - 7, tw, 13, juce::Justification::centred, false); tx += tw + 5; };
        if (s.inA) tag ("A", col::slotA);
        if (s.inB) tag ("B", col::slotB);
        g.setFont (mono (10.0f)); g.setColour (col::text2);
        g.drawText (s.fmt, fmt, juce::Justification::centredLeft, false);
        g.drawText (s.frames, fr, juce::Justification::centredRight, false);
        g.setFont (mono (10.0f, true)); g.setColour (s.kindColour);
        g.drawText (s.kind, lp, juce::Justification::centredRight, false);
        g.setFont (mono (10.0f)); g.setColour (col::text2);
        g.drawText (s.rate, rate, juce::Justification::centredRight, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) rows_.size() && onRowClicked) onRowClicked (rows_[(size_t) row].key);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) rows_.size() && onRowDoubleClicked) onRowDoubleClicked (rows_[(size_t) row].key);
    }
    juce::ListBox list_;
    std::vector<ListRow> rows_;
    juce::String empty_;
    int hover_ = -1;
};

}
