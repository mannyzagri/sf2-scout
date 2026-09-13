// Stock-JUCE widgets styled to docs/handoff-gui-v2/README.md "Widgets". Each
// one paints exactly the geometry the README gives; no behaviour beyond
// hover + click / commit.
#pragma once
#include "Palette.h"
#include <functional>

namespace sf2scout::ui
{

// small uppercase label (README: Sans 500 10 px, letter-spacing 1 px, colour label)
inline void drawSmallLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                            juce::Colour c = col::label, juce::Justification j = juce::Justification::centredLeft)
{
    g.setFont (smallLabel()); g.setColour (c);
    g.drawText (text, r, j, false);        // callers pass the README's exact case ("SEAM 20 ms", preset names)
}

// Badge: 15 x auto, 4 px side pad, radius 2, mono 9 bold #fff on the type colour. Returns its width.
inline int badgeWidth (const juce::String& text) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (badgeFont(), text)) + 8; }
inline int drawBadge (juce::Graphics& g, int x, int y, const juce::String& text, juce::Colour bg, int h = 15)
{
    const int w = badgeWidth (text);
    g.setColour (bg); g.fillRoundedRectangle ((float) x, (float) y, (float) w, (float) h, 2.0f);
    g.setColour (juce::Colours::white); g.setFont (badgeFont());
    g.drawText (text, x, y, w, h, juce::Justification::centred, false);
    return w;
}

// Accent / flat button: fill, optional 1 px control border, radius, hover colour.
class FlatButton : public juce::Button
{
public:
    FlatButton (juce::String text, juce::Colour bg, juce::Colour fg, juce::Colour hover, float radius, juce::Font font, bool border = false)
        : juce::Button (text), bg_ (bg), fg_ (fg), hover_ (hover), radius_ (radius), font_ (font), border_ (border) {}
    void setColours (juce::Colour bg, juce::Colour fg, juce::Colour hover, bool border) { bg_ = bg; fg_ = fg; hover_ = hover; border_ = border; repaint(); }
    void setFont (juce::Font f) { font_ = f; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();      // latching buttons paint accent while on
        g.setColour (on ? ((over || down) ? col::accentHov : col::accent) : ((over || down) ? hover_ : bg_));
        g.fillRoundedRectangle (r, radius_);
        if (border_ && ! on)
        {
            g.setColour (col::control);
            g.drawRoundedRectangle (r.reduced (0.5f), radius_, 1.0f);
        }
        g.setColour (on ? juce::Colours::white : fg_);
        g.setFont (font_);
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
private:
    juce::Colour bg_, fg_, hover_;
    float radius_;
    juce::Font font_;
    bool border_;
};
inline FlatButton* accentButton (const juce::String& text) { return new FlatButton (text, col::accent, juce::Colours::white, col::accentHov, 4.0f, buttonFont()); }
inline FlatButton* flatButton (const juce::String& text, float radius = 4.0f, juce::Font f = buttonFont(), juce::Colour fg = col::text)
{
    return new FlatButton (text, col::inset, fg, col::hoverBtn, radius, f, true);
}

// Segmented: track switchTrack, 1 px control border, radius 4, 1 px inner pad,
// 2 px between cells; on-cell accent / #fff, radius 3; disabled cell text3; a
// selected-but-disabled cell paints thumb. Cell widths come from the manifest.
class SegmentSwitch : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    SegmentSwitch (juce::StringArray labels, std::vector<int> cellWidths, float fontPx = 11.0f)
        : labels_ (std::move (labels)), widths_ (std::move (cellWidths)), fontPx_ (fontPx)
    {
        enabled_.insertMultiple (0, true, labels_.size());
        while ((int) widths_.size() < labels_.size()) widths_.push_back (44);
    }
    std::function<void (int)> onChange;
    void setSegmentEnabled (int i, bool on) { if (i >= 0 && i < enabled_.size() && enabled_[i] != on) { enabled_.set (i, on); repaint(); } }
    bool segmentEnabled (int i) const { return i >= 0 && i < enabled_.size() && enabled_[i]; }
    void setLabel (int i, const juce::String& text) { if (i >= 0 && i < labels_.size() && labels_[i] != text) { labels_.set (i, text); repaint(); } }
    void setIndex (int i, bool notify)
    {
        if (i == index_) return;
        index_ = i;
        repaint();
        if (notify && onChange) onChange (i);
    }
    int index() const { return index_; }
    void setFontPx (float px) { fontPx_ = px; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::switchTrack); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::control);     g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        int x = 2;
        const int cellH = getHeight() - 4;
        // "flex" cells share the remaining width when a width is 0
        int fixed = 0, flex = 0;
        for (int i = 0; i < labels_.size(); ++i) { if (widths_[(size_t) i] > 0) fixed += widths_[(size_t) i]; else ++flex; }
        const int avail = getWidth() - 4 - 2 * (labels_.size() - 1) - fixed;
        for (int i = 0; i < labels_.size(); ++i)
        {
            int w = widths_[(size_t) i];
            if (w <= 0) w = flex > 0 ? avail / flex : 0;
            juce::Rectangle<int> seg (x, 2, w, cellH);
            const bool on = enabled_[i];
            if (i == index_) { g.setColour (on ? col::accent : col::thumb); g.fillRoundedRectangle (seg.toFloat(), 3.0f); }
            g.setColour (i == index_ ? juce::Colours::white : (on ? col::text2 : col::text3));
            g.setFont (segFont (fontPx_));
            g.drawText (labels_[i], seg, juce::Justification::centred, false);
            x += w + 2;
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int x = 2;
        int fixed = 0, flex = 0;
        for (int i = 0; i < labels_.size(); ++i) { if (widths_[(size_t) i] > 0) fixed += widths_[(size_t) i]; else ++flex; }
        const int avail = getWidth() - 4 - 2 * (labels_.size() - 1) - fixed;
        for (int i = 0; i < labels_.size(); ++i)
        {
            int w = widths_[(size_t) i];
            if (w <= 0) w = flex > 0 ? avail / flex : 0;
            if (e.x >= x && e.x < x + w) { if (enabled_[i]) setIndex (i, true); return; }
            x += w + 2;
        }
    }
private:
    juce::StringArray labels_;
    std::vector<int> widths_;
    juce::Array<bool> enabled_;
    float fontPx_;
    int index_ = 0;
};

// Checkbox: box (17, or 15 in the export column), radius 4 (3), control border,
// tick in accent mono 12 bold; label Sans 12 (11) text2.
class Checkbox : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    Checkbox (juce::String label, int box = 17, float labelPx = 12.0f) : label_ (std::move (label)), box_ (box), labelPx_ (labelPx) {}
    std::function<void (bool)> onChange;
    void setToggled (bool on, bool notify) { if (on == on_) return; on_ = on; repaint(); if (notify && onChange) onChange (on_); }
    bool toggled() const { return on_; }
    void paint (juce::Graphics& g) override
    {
        const int pad = box_ == 17 ? 4 : 0;
        juce::Rectangle<float> b ((float) pad, (float) (getHeight() - box_) * 0.5f, (float) box_, (float) box_);
        g.setColour (col::inset);   g.fillRoundedRectangle (b, box_ == 17 ? 4.0f : 3.0f);
        g.setColour (col::control); g.drawRoundedRectangle (b.reduced (0.5f), box_ == 17 ? 4.0f : 3.0f, 1.0f);
        if (on_)
        {
            g.setColour (col::accent); g.setFont (mono (box_ == 17 ? 12.0f : 11.0f, true));
            g.drawText (juce::String::fromUTF8 ("\xE2\x9C\x93"), b.toNearestInt(), juce::Justification::centred, false);
        }
        g.setColour (col::text2); g.setFont (sans (labelPx_));
        g.drawText (label_, getLocalBounds().withTrimmedLeft (pad + box_ + 7), juce::Justification::centredLeft, false);
    }
    void mouseDown (const juce::MouseEvent&) override { setToggled (! on_, true); }
private:
    juce::String label_;
    int box_;
    float labelPx_;
    bool on_ = false;
};

// Footer slider look: 6px track sliderTrack r3, fill accent, thumb 14x16 white 1px thumb r3.
class MasterSliderLook : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (col::sliderTrack); g.fillRoundedRectangle (track, 3.0f);
        g.setColour (col::accent);      g.fillRoundedRectangle (track.withWidth (pos - (float) x), 3.0f);
        juce::Rectangle<float> thumb (pos - 7.0f, cy - 8.0f, 14.0f, 16.0f);
        g.setColour (juce::Colours::white); g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (col::thumb);           g.drawRoundedRectangle (thumb.reduced (0.5f), 3.0f, 1.0f);
    }
    int getSliderThumbRadius (juce::Slider&) override { return 7; }
};

// Chip ComboBox (footer): 28 high, control border, radius 4, mono 12 bold, ▾ in text3.
class ChipComboLook : public juce::LookAndFeel_V4
{
public:
    ChipComboLook()
    {
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::PopupMenu::backgroundColourId, col::inset);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::selRow);
        setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::Rectangle<float> r (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (box.isMouseOver (true) ? col::hoverBtn : col::inset);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::control);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (col::text3); g.setFont (mono (12.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xE2\x96\xBE"), juce::Rectangle<int> (w - 18, 0, 12, h), juce::Justification::centred, false);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (12.0f, true); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (box.getLocalBounds().withTrimmedRight (16));
        label.setJustificationType (juce::Justification::centred);
        label.setFont (mono (12.0f, true));
    }
    juce::Font getPopupMenuFont() override { return mono (12.0f); }
};

// Field: inset fill, 1 px control border, mono 12 bold right-aligned (text
// fields left, mono 12 regular), 4 px side pad; focus border accent. Enter
// commits, Esc reverts, blur commits.
class NumField : public juce::TextEditor
{
public:
    enum class Kind { Integer, Real, Note, Text };
    explicit NumField (Kind kind = Kind::Integer) : kind_ (kind)
    {
        setFont (mono (12.0f, kind != Kind::Text));
        setJustification (kind == Kind::Text ? juce::Justification::centredLeft : juce::Justification::centredRight);
        setIndents (4, 3);
        setColour (juce::TextEditor::backgroundColourId, col::inset);
        setColour (juce::TextEditor::outlineColourId, col::control);
        setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
        setColour (juce::TextEditor::textColourId, col::text);
        setColour (juce::TextEditor::highlightColourId, col::selRow);
        setSelectAllWhenFocused (kind != Kind::Text);
        onReturnKey = [this] { commit(); moveKeyboardFocusToSibling (true); };
        onEscapeKey = [this] { revert(); moveKeyboardFocusToSibling (true); };
        onFocusLost = [this] { if (getText() != shown_) commit(); };
    }
    std::function<void (const juce::String&)> onCommit;   // the owner parses, clamps and calls setShown() back
    void setShown (const juce::String& s) { shown_ = s; if (! hasKeyboardFocus (false)) setText (shown_, false); }
    void setValue (double v, int decimals = 0) { setShown (decimals > 0 ? juce::String (v, decimals) : juce::String ((juce::int64) std::llround (v))); }
    void setTextColour (juce::Colour c) { setColour (juce::TextEditor::textColourId, c); applyColourToAllText (c, true); }
    Kind kind() const { return kind_; }
    bool keyPressed (const juce::KeyPress& k) override { return juce::TextEditor::keyPressed (k) || true; }   // fields swallow keys while focused
private:
    void commit()
    {
        const juce::String t = getText().trim();
        if (onCommit) onCommit (t); else shown_ = t;
        setText (shown_, false);
    }
    void revert() { setText (shown_, false); }
    Kind kind_;
    juce::String shown_;
};

// Read-only field: same box, border hairline; optional warn tint (PERIODS).
class ReadonlyField : public juce::Component
{
public:
    void setText (const juce::String& t, bool warn = false) { if (t != text_ || warn != warn_) { text_ = t; warn_ = warn; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (warn_ ? col::periodsWarnBg : col::inset); g.fillRect (r);
        g.setColour (col::hairline); g.drawRect (r, 1);
        g.setColour (warn_ ? col::warn : col::text); g.setFont (mono (12.0f, true));
        g.drawText (text_, r.reduced (4, 0), juce::Justification::centredRight, false);
    }
private:
    juce::String text_;
    bool warn_ = false;
};

// CLICK METER: three 10 px cells green / yellow / red lit cumulatively, ratio mono 11 bold right.
class ClickMeter : public juce::Component
{
public:
    void set (double ratio, bool valid) { ratio_ = ratio; valid_ = valid; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRect (r);
        g.setColour (col::hairline); g.drawRect (r, 1);
        const int lvl = ! valid_ ? -1 : ratio_ < 1.0 ? 0 : ratio_ <= 2.0 ? 1 : 2;
        auto inner = r.reduced (4, 0);
        auto txt = inner.removeFromRight (30);
        inner.removeFromRight (3);
        const int cellW = (inner.getWidth() - 6) / 3;
        const juce::Colour lit[3] = { col::okDot, col::clickYellow, col::badgeErr };
        for (int i = 0; i < 3; ++i)
        {
            juce::Rectangle<float> c ((float) (inner.getX() + i * (cellW + 3)), (float) (r.getCentreY() - 5), (float) cellW, 10.0f);
            g.setColour (lvl >= i ? lit[i] : col::barBase);
            g.fillRoundedRectangle (c, 2.0f);
        }
        g.setColour (col::text); g.setFont (mono (11.0f, true));
        g.drawText (valid_ ? juce::String (ratio_, 2) : juce::String::fromUTF8 ("\xE2\x80\x94"), txt, juce::Justification::centredRight, false);
    }
private:
    double ratio_ = 0.0;
    bool valid_ = false;
};

// 12 px progress bar: barBase / borderSec, fill accent
class ProgressBarView : public juce::Component
{
public:
    void setFraction (double f) { f = juce::jlimit (0.0, 1.0, f); if (f != frac_) { frac_ = f; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::barBase); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::accent);  g.fillRoundedRectangle (r.reduced (1.0f).withWidth ((float) (r.getWidth() - 2.0f) * (float) frac_), 2.0f);
        g.setColour (col::borderSec); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
private:
    double frac_ = 0.0;
};

}
