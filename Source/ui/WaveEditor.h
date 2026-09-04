// Slot W widgets: the zoomable waveform with draggable loop markers, the seam
// view (loop end butted against loop start), and an integer field that commits
// on Enter. Plain JUCE, function first (docs/SF2SCOUT_WAV_EXTENSION.md).
//
// Data ownership: the editor copies the WAV into ONE shared display buffer
// (mono mix) at load time; both views hold the shared_ptr, so nothing here ever
// points into the engine-owned WavSample the processor's collector may delete.
#pragma once
#include "Palette.h"
#include "../engine/NoteNames.h"
#include "../engine/LoopMarkers.h"
#include <functional>
#include <memory>
#include <vector>

namespace sf2scout::ui
{

using DisplayBuffer = std::shared_ptr<const std::vector<float>>;

namespace wcol
{
    inline const juce::Colour loopStart = hex (0x4c9a5e);    // green marker (spec §2)
    inline const juce::Colour loopEnd   = hex (0xc2453a);    // red marker
    inline const juce::Colour wave      = hex (0x2e5c8a);
    inline const juce::Colour waveDim   = hex (0x9fb3c8);
    inline const juce::Colour playhead  = hex (0xd9772b);
}

// Integer/real text field: commits on Enter (or focus loss), reverts on Escape.
class NumField : public juce::TextEditor
{
public:
    std::function<void (double)> onCommit;
    NumField (bool integer = true) : integer_ (integer)
    {
        setFont (mono (12.0f, true));
        setJustification (juce::Justification::centredRight);
        setIndents (4, 3);
        setColour (juce::TextEditor::backgroundColourId, col::inset);
        setColour (juce::TextEditor::outlineColourId, col::control);
        setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
        setColour (juce::TextEditor::textColourId, col::text);
        setSelectAllWhenFocused (true);
        onReturnKey = [this] { commit(); };
        onEscapeKey = [this] { revert(); moveKeyboardFocusToSibling (true); };
        onFocusLost = [this] { if (getText() != shown_) commit(); };
    }
    void setValue (double v)
    {
        value_ = v;
        shown_ = integer_ ? juce::String ((juce::int64) std::llround (v)) : juce::String (v, 1);
        if (! hasKeyboardFocus (false)) setText (shown_, false);
    }
    double value() const { return value_; }
private:
    void commit()
    {
        const juce::String t = getText().trim();
        if (t.isEmpty() || ! (t.containsOnly ("0123456789.-+"))) { revert(); return; }
        const double v = t.getDoubleValue();
        if (onCommit) onCommit (v);      // the owner clamps and calls setValue() back
        else setValue (v);
        setText (shown_, false);
    }
    void revert() { setText (shown_, false); }
    bool integer_;
    double value_ = 0.0;
    juce::String shown_;
};

// ----------------------------------------------------------------- waveform
class WaveformView : public juce::Component
{
public:
    std::function<void (juce::int64 start, juce::int64 end)> onLoopDragged;   // during a marker drag (already snapped)
    std::function<void (juce::int64 frame)> onCursor;                         // mouse position in frames (-1 = outside)
    std::function<void()> onWantsFocus;                                        // click -> give the editor the keyboard

    // Marker interaction (root cause of the 0.2.0 "cannot drag" report: the ONLY
    // placement path was grabbing a 2 px line within +-6 px, and the default
    // whole-file loop puts those lines at x = 0 and x = width -- half clipped by
    // the border -- or off-screen once zoomed in):
    //   left click/drag        near a line (+-kGrabPx) -> drag that marker
    //                          elsewhere               -> the NEARER marker jumps to the click and follows
    //   right click/drag       -> always the END marker
    //   shift-drag / middle    -> pan
    static constexpr double kGrabPx = 8.0;
    WaveformView() { setInterceptsMouseClicks (true, false); setMouseClickGrabsKeyboardFocus (false); }

    void setBuffer (DisplayBuffer b, double rate)
    {
        buf_ = std::move (b);
        rate_ = rate;
        frames_ = buf_ ? (juce::int64) buf_->size() : 0;
        viewStart_ = 0.0;
        viewLen_ = (double) juce::jmax<juce::int64> (1, frames_);
        repaint();
    }
    void setLoop (juce::int64 start, juce::int64 end) { if (start != loopStart_ || end != loopEnd_) { loopStart_ = start; loopEnd_ = end; repaint(); } }
    void setPlayhead (double frame) { if (frame != playhead_) { playhead_ = frame; repaint(); } }
    void setSnap (bool on) { snap_ = on; }
    bool snap() const { return snap_; }
    void setLoopEnabled (bool on) { if (on != loopOn_) { loopOn_ = on; repaint(); } }
    juce::int64 frames() const { return frames_; }

    // nearest zero crossing to `frame` within +-window (unchanged if none)
    juce::int64 snapToZero (juce::int64 frame, juce::int64 window = 2000) const
    {
        if (! buf_ || frames_ < 3) return frame;
        const auto& b = *buf_;
        juce::int64 best = frame; juce::int64 bestD = window + 1;
        const juce::int64 lo = juce::jmax<juce::int64> (1, frame - window), hi = juce::jmin (frames_ - 1, frame + window);
        for (juce::int64 i = lo; i <= hi; ++i)
        {
            const float a = b[(size_t) i - 1], c = b[(size_t) i];
            if ((a < 0.0f && c >= 0.0f) || (a > 0.0f && c <= 0.0f) || c == 0.0f)
            {
                const juce::int64 d = std::llabs (i - frame);
                if (d < bestD) { bestD = d; best = i; }
            }
        }
        return best;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        if (! buf_ || frames_ == 0)
        {
            g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setFont (mono (12.0f)); g.setColour (col::text3);
            g.drawText ("drop a .wav here or LOAD WAV", r, juce::Justification::centred, false);
            return;
        }
        const auto& b = *buf_;
        const int w = r.getWidth(), h = r.getHeight();
        const float mid = (float) r.getCentreY();
        const float half = (float) h * 0.46f;
        // loop tint
        if (loopOn_)
        {
            const float x0 = xOf ((double) loopStart_), x1 = xOf ((double) loopEnd_ + 1.0);
            g.setColour (col::loopFill.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (juce::jmax (0.0f, x0), (float) r.getY(), juce::jmin ((float) w, x1) - juce::jmax (0.0f, x0), (float) h));
        }
        // centre line
        g.setColour (col::hairline); g.fillRect (0, (int) mid, w, 1);
        // min/max per column
        const double fpp = viewLen_ / (double) w;
        g.setColour (wcol::wave);
        for (int x = 0; x < w; ++x)
        {
            const double f0 = viewStart_ + x * fpp, f1 = f0 + fpp;
            juce::int64 i0 = (juce::int64) std::floor (f0), i1 = (juce::int64) std::ceil (f1);
            i0 = juce::jlimit<juce::int64> (0, frames_ - 1, i0); i1 = juce::jlimit<juce::int64> (i0, frames_ - 1, i1);
            float mn = 1.0f, mx = -1.0f;
            for (juce::int64 i = i0; i <= i1; ++i) { const float v = b[(size_t) i]; mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
            if (fpp < 1.0)
            {
                // sample level: draw the interpolated value as a step and a dot
                const float v = b[(size_t) i0];
                g.fillRect (juce::Rectangle<float> ((float) x, mid - v * half - 1.0f, 1.0f, 2.0f));
                if (fpp < 0.25 && std::fabs (f0 - std::round (f0)) < fpp) g.fillEllipse ((float) x - 1.5f, mid - v * half - 1.5f, 3.0f, 3.0f);
            }
            else
            {
                const float yTop = mid - mx * half, yBot = mid - mn * half;
                g.fillRect (juce::Rectangle<float> ((float) x, yTop, 1.0f, juce::jmax (1.0f, yBot - yTop)));
            }
        }
        // markers
        drawMarker (g, (double) loopStart_, wcol::loopStart, "START", true);
        drawMarker (g, (double) loopEnd_ + 1.0, wcol::loopEnd, "END", false);
        // playhead
        if (playhead_ >= 0.0)
        {
            const float px = xOf (playhead_);
            if (px >= 0.0f && px <= (float) w) { g.setColour (wcol::playhead); g.fillRect (juce::Rectangle<float> (px - 1.0f, (float) r.getY(), 2.0f, (float) h)); }
        }
        // zoom readout
        g.setFont (mono (10.0f)); g.setColour (col::text3);
        g.drawText (juce::String ((juce::int64) viewStart_) + " .. " + juce::String ((juce::int64) (viewStart_ + viewLen_)) + "  (" + juce::String (fpp, fpp < 10 ? 2 : 0) + " smp/px)",
                    r.reduced (6, 2), juce::Justification::bottomRight, false);
        g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }

    // mouse: wheel = zoom about cursor, shift-drag = pan, drag near a marker = move it
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override
    {
        if (frames_ == 0) return;
        const double anchor = frameAt (e.x);
        const double factor = std::pow (1.25, -d.deltaY * 4.0);
        double newLen = juce::jlimit (16.0, (double) frames_, viewLen_ * factor);
        const double frac = (double) e.x / (double) juce::jmax (1, getWidth());
        viewStart_ = juce::jlimit (0.0, (double) frames_ - newLen, anchor - frac * newLen);
        viewLen_ = newLen;
        repaint();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (onCursor) onCursor (frames_ > 0 ? juce::jlimit<juce::int64> (0, frames_ - 1, (juce::int64) frameAt (e.x)) : -1);
        setMouseCursor (markerAt (e.x, true) != Marker::None ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (onCursor) onCursor (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onWantsFocus) onWantsFocus();
        if (frames_ == 0) return;
        panStart_ = viewStart_;
        if (e.mods.isShiftDown() || e.mods.isMiddleButtonDown()) { dragging_ = Marker::None; panning_ = true; return; }
        panning_ = false;
        if (e.mods.isRightButtonDown()) { dragging_ = Marker::End; moveDragged (e.x); return; }
        dragging_ = markerAt (e.x, true);                     // grabbed an existing line: it follows from here
        if (dragging_ == Marker::None) { dragging_ = markerAt (e.x, false); moveDragged (e.x); }   // click-to-place the nearer one
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (frames_ == 0) return;
        if (panning_)
        {
            const double fpp = viewLen_ / (double) juce::jmax (1, getWidth());
            viewStart_ = juce::jlimit (0.0, (double) frames_ - viewLen_, panStart_ - e.getDistanceFromDragStartX() * fpp);
            repaint();
            return;
        }
        moveDragged (e.x);
    }
    void mouseUp (const juce::MouseEvent&) override { dragging_ = Marker::None; panning_ = false; }

private:
    float xOf (double frame) const { return (float) ((frame - viewStart_) / viewLen_ * (double) getWidth()); }
    double frameAt (int x) const { return viewStart_ + (double) x / (double) juce::jmax (1, getWidth()) * viewLen_; }
    Marker markerAt (int x, bool strict) const
    {
        if (frames_ == 0) return Marker::None;
        return pickMarker (xOf ((double) loopStart_), xOf ((double) loopEnd_ + 1.0), (double) x, kGrabPx, strict);
    }
    // moves the dragged marker to the frame under pixel x (snapped), clamped against the other marker
    void moveDragged (int x)
    {
        if (dragging_ == Marker::None) return;
        juce::int64 f = juce::jlimit<juce::int64> (0, frames_, (juce::int64) std::llround (frameAt (x)));
        if (snap_)
        {
            // snap the frame the line represents (END's line is one past the last included sample)
            const juce::int64 win = juce::jmax<juce::int64> (4, (juce::int64) (viewLen_ / juce::jmax (1, getWidth()) * 6.0));
            f = snapToZero (juce::jlimit<juce::int64> (0, frames_ - 1, f), win);
            if (dragging_ == Marker::End) f = juce::jmin (frames_, f + 1);
        }
        juce::int64 s = loopStart_, en = loopEnd_;
        applyMarkerDrag (dragging_, f, s, en, frames_ - 1);
        if (s != loopStart_ || en != loopEnd_)
        {
            loopStart_ = s; loopEnd_ = en; repaint();          // instant visual feedback; the owner echoes the same values back
            if (onLoopDragged) onLoopDragged (s, en);
        }
        if (onCursor) onCursor (juce::jlimit<juce::int64> (0, frames_ - 1, f));
    }
    void drawMarker (juce::Graphics& g, double frame, juce::Colour c, const char* label, bool labelRight)
    {
        float x = xOf (frame);
        if (x < -1.0f || x > (float) getWidth() + 1.0f) return;
        x = juce::jlimit (1.0f, (float) getWidth() - 1.0f, x);    // a marker on the file edge stays visible inside the border
        g.setColour (c);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) getHeight()));
        g.setFont (mono (9.0f, true));
        juce::Rectangle<int> tag ((int) x + (labelRight ? 3 : -38), 3, 35, 11);
        g.drawText (label, tag, labelRight ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }

    DisplayBuffer buf_;
    double rate_ = 44100.0;
    juce::int64 frames_ = 0;
    double viewStart_ = 0.0, viewLen_ = 1.0;
    juce::int64 loopStart_ = 0, loopEnd_ = 0;
    double playhead_ = -1.0;
    bool snap_ = true, loopOn_ = true;
    Marker dragging_ = Marker::None;
    bool panning_ = false;
    double panStart_ = 0.0;
};

// ----------------------------------------------------------------- seam view
// Left half: the last N ms before (and including) loopEnd. Right half: the
// first N ms from loopStart. The centre line is the splice the loop plays.
class SeamView : public juce::Component
{
public:
    void setBuffer (DisplayBuffer b, double rate) { buf_ = std::move (b); rate_ = rate; repaint(); }
    void setLoop (juce::int64 start, juce::int64 end) { if (start != loopStart_ || end != loopEnd_) { loopStart_ = start; loopEnd_ = end; repaint(); } }
    void setPlayhead (double f) { if (f != playhead_) { playhead_ = f; repaint(); } }
    void setWindowMs (double ms) { windowMs_ = juce::jlimit (1.0, 200.0, ms); repaint(); }
    double windowMs() const { return windowMs_; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& d) override { setWindowMs (windowMs_ * std::pow (1.25, -d.deltaY * 4.0)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setFont (smallLabel()); g.setColour (col::label);
        g.drawText ("SEAM  " + juce::String (windowMs_, 0) + " ms", r.reduced (6, 2), juce::Justification::topLeft, false);
        if (! buf_ || buf_->empty() || loopEnd_ < loopStart_) return;
        const auto& b = *buf_;
        const juce::int64 n = (juce::int64) b.size();
        const int w = r.getWidth(), h = r.getHeight();
        const float mid = (float) r.getCentreY() + 4.0f, half = (float) h * 0.38f;
        const juce::int64 win = juce::jmax<juce::int64> (2, (juce::int64) (windowMs_ * 0.001 * rate_));
        const int cx = w / 2;
        auto sampleAt = [&] (juce::int64 i) -> float { return (i >= 0 && i < n) ? b[(size_t) i] : 0.0f; };
        g.setColour (col::hairline); g.fillRect (0, (int) mid, w, 1);
        juce::Path pl, pr;
        for (int x = 0; x < cx; ++x)
        {
            // pixel x on the left half covers frames leading up to loopEnd (inclusive at the seam)
            const double t = (double) (cx - x) / (double) cx;                    // 1 at far left, ->0 at the seam
            const juce::int64 i = loopEnd_ - (juce::int64) std::llround (t * (double) win) + 0;
            const float v = sampleAt (i);
            const float y = mid - v * half;
            if (x == 0) pl.startNewSubPath ((float) x, y); else pl.lineTo ((float) x, y);
        }
        pl.lineTo ((float) cx, mid - sampleAt (loopEnd_) * half);
        for (int x = cx; x < w; ++x)
        {
            const double t = (double) (x - cx) / (double) juce::jmax (1, w - cx);
            const juce::int64 i = loopStart_ + (juce::int64) std::llround (t * (double) win);
            const float y = mid - sampleAt (i) * half;
            if (x == cx) pr.startNewSubPath ((float) x, y); else pr.lineTo ((float) x, y);
        }
        g.setColour (wcol::loopEnd.withAlpha (0.9f));   g.strokePath (pl, juce::PathStrokeType (1.2f));
        g.setColour (wcol::loopStart.withAlpha (0.9f)); g.strokePath (pr, juce::PathStrokeType (1.2f));
        g.setColour (col::text2); g.fillRect (cx, 14, 1, h - 14);
        // the discontinuity, in dB-ish terms: |end sample - start sample|
        const float jump = std::fabs (sampleAt (loopEnd_) - sampleAt (loopStart_));
        g.setFont (mono (10.0f)); g.setColour (jump > 0.05f ? col::warn : col::text2);
        g.drawText ("step " + juce::String (jump, 3), r.reduced (6, 2), juce::Justification::topRight, false);
        // playhead when it is inside either window
        if (playhead_ >= 0.0)
        {
            float px = -1.0f;
            if (playhead_ <= (double) loopEnd_ && playhead_ > (double) (loopEnd_ - win))
                px = (float) cx - (float) (((double) loopEnd_ - playhead_) / (double) win * cx);
            else if (playhead_ >= (double) loopStart_ && playhead_ < (double) (loopStart_ + win))
                px = (float) cx + (float) ((playhead_ - (double) loopStart_) / (double) win * (w - cx));
            if (px >= 0.0f) { g.setColour (wcol::playhead); g.fillRect (juce::Rectangle<float> (px - 1.0f, 14.0f, 2.0f, (float) h - 14.0f)); }
        }
    }
private:
    DisplayBuffer buf_;
    double rate_ = 44100.0, windowMs_ = 20.0, playhead_ = -1.0;
    juce::int64 loopStart_ = 0, loopEnd_ = 0;
};

}
