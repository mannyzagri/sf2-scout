// The A | B readout columns, the zone map strip and its label row -- README
// 2.3 and 3, drawn with paint() from VALUE snapshots the editor builds.
#pragma once
#include "Widgets.h"
#include "../engine/NoteNames.h"
#include <vector>

namespace sf2scout::ui
{

inline juce::String dash() { return juce::String::fromUTF8 ("\xE2\x80\x94"); }
inline juce::String enDash() { return juce::String::fromUTF8 ("\xE2\x80\x93"); }

// ----------------------------------------------------------------- readout column
struct ReadoutData
{
    bool empty = true;
    juce::String slot = "A";
    juce::Colour slotBg = col::slotA;
    juce::String badge = dash();
    juce::Colour badgeBg = col::idleDot;
    juce::String path;
    bool live = false;
    juce::String played = dash(), root = dash(), fine, stretch = dash();
    juce::Colour stretchColour = col::text;
    struct Row { juce::String k, v; juce::Colour c; };
    std::vector<Row> rows;
    juce::String emptyMsg;
    bool borderRight = false;           // column A: 14 px right padding + 1 px hairline
};

class ReadoutColumn : public juce::Component
{
public:
    void set (const ReadoutData& d) { d_ = d; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        if (d_.borderRight)
        {
            g.setColour (col::hairline); g.fillRect (area.getRight() - 1, 0, 1, area.getHeight());
            area.removeFromRight (15);
        }
        // row 16: slot square, badge, path, live dot
        auto row = area.removeFromTop (16);
        g.setColour (d_.slotBg); g.fillRoundedRectangle (row.removeFromLeft (16).toFloat(), 3.0f);
        g.setColour (juce::Colours::white); g.setFont (mono (11.0f, true));
        g.drawText (d_.slot, 0, 0, 16, 16, juce::Justification::centred, false);
        row.removeFromLeft (6);
        const int bw = drawBadge (g, row.getX(), row.getY() + 1, d_.badge, d_.badgeBg, 14);
        row.removeFromLeft (bw + 6);
        auto dot = row.removeFromRight (7);
        g.setColour (d_.live ? col::okDot : col::idleDot);
        g.fillEllipse ((float) dot.getX(), (float) row.getCentreY() - 3.5f, 7.0f, 7.0f);
        row.removeFromRight (6);
        g.setColour (col::text2); g.setFont (mono (11.0f));
        g.drawText (d_.path, row, juce::Justification::centredLeft, true);
        area.removeFromTop (12);
        // big row 38: PLAYED · ROOT (+ fine) · STRETCH, gap 10
        auto big = area.removeFromTop (38);
        int x = big.getX();
        auto field = [&] (const juce::String& label, const juce::String& value, juce::Colour c, const juce::String& suffix)
        {
            const juce::Font vf = mono (20.0f, true), sf = mono (11.0f, true);
            const int vw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (vf, value));
            const int sw = suffix.isEmpty() ? 0 : 3 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (sf, suffix));
            const int lw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (smallLabel(), label));
            const int w = std::max (vw + sw, lw);
            drawSmallLabel (g, label, juce::Rectangle<int> (x, big.getY(), w + 2, 12));
            g.setFont (vf); g.setColour (c);
            g.drawText (value, x, big.getY() + 14, vw + 2, 24, juce::Justification::centredLeft, false);
            if (! suffix.isEmpty()) { g.setFont (sf); g.setColour (col::text2); g.drawText (suffix, x + vw + 3, big.getY() + 14, sw, 24, juce::Justification::centredLeft, false); }
            x += w + 10;
        };
        field ("PLAYED", d_.played, col::accent, {});
        field ("ROOT", d_.root, col::text, d_.fine);
        field ("STRETCH", d_.stretch, d_.stretchColour, {});
        area.removeFromTop (12);
        if (d_.empty)
        {
            area.removeFromTop (14);
            g.setColour (col::text3); g.setFont (sans (11.0f));
            g.drawFittedText (d_.emptyMsg, area.withHeight (120), juce::Justification::topLeft, 8, 1.0f);
            return;
        }
        // rows 17 high mono 11: key 66 wide label colour, value ellipsised
        g.setFont (mono (11.0f));
        for (const auto& r : d_.rows)
        {
            auto line = area.removeFromTop (17);
            g.setColour (col::label); g.drawText (r.k, line.removeFromLeft (66), juce::Justification::centredLeft, false);
            g.setColour (r.c);        g.drawText (r.v, line, juce::Justification::centredLeft, true);
        }
    }
private:
    ReadoutData d_;
};

// ----------------------------------------------------------------- zone map
struct ZoneCell { int lo = 0, hi = 127, root = 60; juce::String name; };

class ZoneMapStrip : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onKeyDown;    // audition
    std::function<void (int)> onKeyUp;

    void setZones (std::vector<ZoneCell> zones, int selectedKey, int splitKey)
    {
        zones_ = std::move (zones); selected_ = selectedKey; split_ = splitKey;
        for (int n = 0; n < 128; ++n)
        {
            zoneOfKey_[n] = -1;
            for (size_t i = 0; i < zones_.size(); ++i)
                if (n >= zones_[i].lo && n <= zones_[i].hi) { zoneOfKey_[n] = (int) i; break; }
        }
        repaint();
    }
    int zoneCount() const { return (int) zones_.size(); }
    int zoneOf (int n) const { return (n >= 0 && n < 128) ? zoneOfKey_[n] : -1; }
    const std::vector<ZoneCell>& zones() const { return zones_; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float cw = (float) r.getWidth() / 128.0f;
        for (int n = 0; n < 128; ++n)
        {
            juce::Rectangle<float> cell ((float) r.getX() + n * cw, (float) r.getY(), cw, (float) r.getHeight());
            const int zi = zoneOfKey_[n];
            const bool sel = n == selected_, isSplit = n == split_;
            juce::Colour bg = sel ? col::accent : isSplit ? col::warn : (zones_.empty() ? col::zoneB : (zi < 0 ? col::inset : ((zi % 2) == 0 ? col::zoneA : col::zoneB)));
            g.setColour (bg); g.fillRect (cell);
            if (isBlack (n))
            {
                g.setColour (juce::Colour (0x4d1c1c1a));
                g.fillRect (cell.withHeight (cell.getHeight() * 0.58f));
            }
            if (n % 12 == 0)
            {
                g.setColour (juce::Colour (0x591c1c1a));
                g.fillRect (cell.withTop (cell.getBottom() - 4.0f));
            }
            if (zi >= 0 && zones_[(size_t) zi].hi == n)
            {
                g.setColour (col::zoneEdge);
                g.fillRect (juce::Rectangle<float> (cell.getRight() - 1.0f, cell.getY(), 1.0f, cell.getHeight()));
            }
        }
        g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int n = keyAt (e.x);
        const int zi = zoneOfKey_[n];
        setTooltip (juce::String::fromUTF8 (noteName (n).c_str()) + (zi >= 0 ? juce::String::fromUTF8 (" \xC2\xB7 ") + zones_[(size_t) zi].name : juce::String()));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        held_ = keyAt (e.x);
        if (onKeyDown) onKeyDown (held_);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (held_ >= 0 && onKeyUp) onKeyUp (held_);
        held_ = -1;
    }
private:
    static bool isBlack (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    int keyAt (int x) const { return juce::jlimit (0, 127, (int) (x * 128 / juce::jmax (1, getWidth()))); }
    std::vector<ZoneCell> zones_;
    int selected_ = -1, split_ = -1, held_ = -1;
    int zoneOfKey_[128] {};
};

// Zone labels row beneath the strip: one block per zone, width by key count.
class ZoneLabelsRow : public juce::Component
{
public:
    void setZones (std::vector<ZoneCell> zones) { zones_ = std::move (zones); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const float cw = (float) getWidth() / 128.0f;
        for (const auto& z : zones_)
        {
            juce::Rectangle<int> block ((int) std::floor (z.lo * cw), 0, (int) std::floor ((z.hi - z.lo + 1) * cw), getHeight());
            g.setColour (col::control); g.fillRect (block.getX(), 0, 1, getHeight());
            auto text = block.withTrimmedLeft (6).withTrimmedRight (4);
            g.setFont (mono (10.0f));
            g.setColour (col::text2);
            g.drawText (z.name, text.removeFromTop (10), juce::Justification::centredLeft, true);
            g.setColour (col::text3);
            g.drawText (juce::String::fromUTF8 (noteName (z.lo).c_str()) + enDash() + juce::String::fromUTF8 (noteName (z.hi).c_str()) + "  root " + juce::String::fromUTF8 (noteName (z.root).c_str()),
                        text.removeFromTop (10), juce::Justification::centredLeft, true);
        }
    }
private:
    std::vector<ZoneCell> zones_;
};

}
