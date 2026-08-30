// Small stock-JUCE widgets styled to the handoff. Each one paints exactly the
// geometry the README gives; no behaviour beyond hover + click.
#pragma once
#include "Palette.h"
#include <functional>

namespace sf2scout::ui
{

// LOAD / stepper buttons: flat rect, radius given, hover colour.
class FlatButton : public juce::Button
{
public:
    FlatButton (juce::String text, juce::Colour bg, juce::Colour fg, juce::Colour hover, float radius, juce::Font font, bool border = false)
        : juce::Button (text), bg_ (bg), fg_ (fg), hover_ (hover), radius_ (radius), font_ (font), border_ (border) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour ((over || down) ? hover_ : bg_);
        g.fillRoundedRectangle (r, radius_);
        if (border_)
        {
            g.setColour (col::control);
            g.drawRoundedRectangle (r.reduced (0.5f), radius_, 1.0f);
        }
        g.setColour (fg_);
        g.setFont (font_);
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
private:
    juce::Colour bg_, fg_, hover_;
    float radius_;
    juce::Font font_;
    bool border_;
};

// 2-segment switch (MODE): track #e2e2db, 1px #d0d0c8, r4, 2px pad, 2px gap.
class SegmentSwitch : public juce::Component
{
public:
    SegmentSwitch (juce::StringArray labels) : labels_ (std::move (labels)) {}
    std::function<void (int)> onChange;
    void setIndex (int i, bool notify)
    {
        if (i == index_) return;
        index_ = i;
        repaint();
        if (notify && onChange) onChange (i);
    }
    int index() const { return index_; }
    int preferredWidth() const
    {
        int w = 2 * 2 + 2 * (labels_.size() - 1);
        for (auto& l : labels_) w += segWidth (l);
        return w;
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::switchTrack); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::control);     g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        int x = 2;
        for (int i = 0; i < labels_.size(); ++i)
        {
            const int w = segWidth (labels_[i]);
            juce::Rectangle<int> seg (x, 2, w, getHeight() - 4);
            if (i == index_) { g.setColour (col::accent); g.fillRoundedRectangle (seg.toFloat(), 3.0f); }
            g.setColour (i == index_ ? juce::Colours::white : col::text2);
            g.setFont (segFont());
            g.drawText (labels_[i], seg, juce::Justification::centred, false);
            x += w + 2;
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int x = 2;
        for (int i = 0; i < labels_.size(); ++i)
        {
            const int w = segWidth (labels_[i]);
            if (e.x >= x && e.x < x + w) { setIndex (i, true); return; }
            x += w + 2;
        }
    }
private:
    static juce::Font segFont() { juce::Font f = mono (11.0f, true); f.setExtraKerningFactor (0.04f); return f; }
    static int segWidth (const juce::String& s)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (segFont(), s)) + 24;   // padding 7px 12px
    }
    juce::StringArray labels_;
    int index_ = 0;
};

// Footer slider look: 6px track #dcdcd4 r3, fill accent, thumb 14x16 white 1px #b6b6ae r3.
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

// MIDI chip ComboBox: white, 1px #d0d0c8, r4, mono 500 12px, hover #eef2f7.
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
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (12.0f, true); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (box.getLocalBounds());
        label.setJustificationType (juce::Justification::centred);
        label.setFont (mono (12.0f, true));
    }
    juce::Font getPopupMenuFont() override { return mono (12.0f); }
};

}
