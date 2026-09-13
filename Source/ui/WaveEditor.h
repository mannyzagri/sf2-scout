// Editor displays: the zoomable waveform with draggable loop markers and the
// seam view (loop end butted against loop start) -- README 4. Plain JUCE.
//
// Data ownership: the editor hands both views ONE shared display buffer (mono
// mix) per sample; nothing here points into the engine-owned WavSample.
#pragma once
#include "Widgets.h"
#include "../engine/LoopMarkers.h"
#include <memory>
#include <vector>

namespace sf2scout::ui
{

using DisplayBuffer = std::shared_ptr<const std::vector<float>>;

// ----------------------------------------------------------------- waveform
class WaveformView : public juce::Component
{
public:
    std::function<void (juce::int64 start, juce::int64 end)> onLoopDragged;   // during and at the end of a marker drag (snapped on release)
    std::function<void (juce::int64 frame)> onCursor;                         // mouse position in frames (-1 = outside)
    std::function<void()> onWantsFocus;                                        // click -> give the editor the keyboard

    // Marker interaction (README 4): left click/drag moves the NEARER marker
    // (a line within +-kGrabPx is grabbed), right-drag always END, wheel zooms
    // about the cursor (min 64 frames), shift-drag or middle-drag pans; on
    // release with ZERO-X SNAP the marker snaps to the nearest rising zero
    // crossing within +-64 frames.
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
    juce::int64 viewStart() const { return (juce::int64) viewStart_; }
    juce::int64 viewEnd() const { return (juce::int64) (viewStart_ + viewLen_); }

    // nearest RISING zero crossing to `frame` within +-window (unchanged if none)
    juce::int64 snapRising (juce::int64 frame, juce::int64 window = 64) const
    {
        if (! buf_ || frames_ < 3) return frame;
        const auto& b = *buf_;
        juce::int64 best = frame; juce::int64 bestD = window + 1;
        const juce::int64 lo = juce::jmax<juce::int64> (1, frame - window), hi = juce::jmin (frames_ - 1, frame + window);
        for (juce::int64 i = lo; i <= hi; ++i)
        {
            const float a = b[(size_t) i - 1], c = b[(size_t) i];
            if (a <= 0.0f && c > 0.0f)
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
            g.setFont (mono (10.0f)); g.setColour (col::text3);
            g.drawText ("drop a source or pick a sample", r.reduced (6, 4), juce::Justification::bottomRight, false);
            return;
        }
        const auto& b = *buf_;
        const int w = r.getWidth(), h = r.getHeight();
        const float mid = (float) r.getCentreY();
        const float half = (float) h * 0.46f;
        if (loopOn_)
        {
            const float x0 = xOf ((double) loopStart_), x1 = xOf ((double) loopEnd_ + 1.0);
            g.setColour (col::loopFill.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (juce::jmax (0.0f, x0), (float) r.getY(), juce::jmin ((float) w, x1) - juce::jmax (0.0f, x0), (float) h));
        }
        g.setColour (col::hairline); g.fillRect (0, (int) mid, w, 1);
        const double fpp = viewLen_ / (double) w;
        g.setColour (col::wave);
        for (int x = 0; x < w; ++x)
        {
            const double f0 = viewStart_ + x * fpp, f1 = f0 + fpp;
            juce::int64 i0 = (juce::int64) std::floor (f0), i1 = (juce::int64) std::ceil (f1);
            i0 = juce::jlimit<juce::int64> (0, frames_ - 1, i0); i1 = juce::jlimit<juce::int64> (i0, frames_ - 1, i1);
            float mn = 1.0f, mx = -1.0f;
            for (juce::int64 i = i0; i <= i1; ++i) { const float v = b[(size_t) i]; mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
            if (fpp < 1.0)
            {
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
        drawMarker (g, (double) loopStart_, col::mStart, "START", true);
        drawMarker (g, (double) loopEnd_ + 1.0, col::mEnd, "END", false);
        if (playhead_ >= 0.0)
        {
            const float px = xOf (playhead_);
            if (px >= 0.0f && px <= (float) w) { g.setColour (col::playhead); g.fillRect (juce::Rectangle<float> (px - 1.0f, (float) r.getY(), 2.0f, (float) h)); }
        }
        g.setFont (mono (10.0f)); g.setColour (col::text3);
        g.drawText (juce::String ((juce::int64) viewStart_) + " .. " + juce::String ((juce::int64) (viewStart_ + viewLen_)) + "  (" + juce::String (fpp, 1) + " smp/px)",
                    r.reduced (6, 4), juce::Justification::bottomRight, false);
        g.drawText (viewLen_ < (double) frames_ ? juce::String::fromUTF8 ("wheel zoom \xC2\xB7 shift-drag pan") : juce::String ("wheel to zoom"),
                    r.reduced (6, 4), juce::Justification::bottomLeft, false);
        g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override
    {
        if (frames_ == 0) return;
        const double anchor = frameAt (e.x);
        const double factor = std::pow (1.25, -d.deltaY * 4.0);
        double newLen = juce::jlimit (64.0, (double) frames_, viewLen_ * factor);
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
        dragging_ = markerAt (e.x, true);
        if (dragging_ == Marker::None) { dragging_ = markerAt (e.x, false); moveDragged (e.x); }
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
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging_ != Marker::None && snap_ && frames_ > 2)
        {
            juce::int64 s = loopStart_, en = loopEnd_;
            if (dragging_ == Marker::Start) s = juce::jlimit<juce::int64> (0, en, snapRising (s));
            else en = juce::jlimit<juce::int64> (s, frames_ - 1, snapRising (en + 1) - 1);
            if (s != loopStart_ || en != loopEnd_)
            {
                loopStart_ = s; loopEnd_ = en; repaint();
                if (onLoopDragged) onLoopDragged (s, en);
            }
        }
        dragging_ = Marker::None; panning_ = false;
    }

private:
    float xOf (double frame) const { return (float) ((frame - viewStart_) / viewLen_ * (double) getWidth()); }
    double frameAt (int x) const { return viewStart_ + (double) x / (double) juce::jmax (1, getWidth()) * viewLen_; }
    Marker markerAt (int x, bool strict) const
    {
        if (frames_ == 0) return Marker::None;
        return pickMarker (xOf ((double) loopStart_), xOf ((double) loopEnd_ + 1.0), (double) x, kGrabPx, strict);
    }
    void moveDragged (int x)
    {
        if (dragging_ == Marker::None) return;
        const juce::int64 f = juce::jlimit<juce::int64> (0, frames_, (juce::int64) std::llround (frameAt (x)));
        juce::int64 s = loopStart_, en = loopEnd_;
        applyMarkerDrag (dragging_, f, s, en, frames_ - 1);
        if (s != loopStart_ || en != loopEnd_)
        {
            loopStart_ = s; loopEnd_ = en; repaint();
            if (onLoopDragged) onLoopDragged (s, en);
        }
        if (onCursor) onCursor (juce::jlimit<juce::int64> (0, frames_ - 1, f));
    }
    void drawMarker (juce::Graphics& g, double frame, juce::Colour c, const char* label, bool labelRight)
    {
        float x = xOf (frame);
        if (x < -1.0f || x > (float) getWidth() + 1.0f) return;
        x = juce::jlimit (1.0f, (float) getWidth() - 1.0f, x);
        g.setColour (c);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) getHeight()));
        g.setFont (mono (9.0f, true));
        juce::Rectangle<int> tag ((int) x + (labelRight ? 3 : -40), 3, 36, 11);
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
// Left half: the last N/2 ms before (and including) loopEnd, in mEnd. Right
// half: the first N/2 ms from loopStart, in mStart. The centre line is the splice.
class SeamView : public juce::Component
{
public:
    void setBuffer (DisplayBuffer b, double rate) { buf_ = std::move (b); rate_ = rate; repaint(); }
    void setLoop (juce::int64 start, juce::int64 end) { if (start != loopStart_ || end != loopEnd_) { loopStart_ = start; loopEnd_ = end; repaint(); } }
    void setPlayhead (double f) { if (f != playhead_) { playhead_ = f; repaint(); } }
    void setWindowMs (double ms) { windowMs_ = juce::jlimit (10.0, 100.0, ms); repaint(); }
    double windowMs() const { return windowMs_; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& d) override { setWindowMs (windowMs_ * std::pow (1.25, -d.deltaY * 4.0)); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (col::borderOut); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        drawSmallLabel (g, "SEAM  " + juce::String (windowMs_, 0) + " ms", juce::Rectangle<int> (6, 3, 120, 12));
        const int w = r.getWidth(), h = r.getHeight();
        const float mid = 46.0f, amp = 28.0f;
        const int cx = w / 2;
        g.setColour (col::hairline); g.fillRect (0, (int) mid, w, 1);
        g.setColour (col::text2); g.fillRect (cx - 1, 16, 1, h - 16);
        if (! buf_ || buf_->empty() || loopEnd_ < loopStart_)
        {
            g.setFont (mono (10.0f)); g.setColour (col::text2);
            g.drawText ("step " + juce::String::fromUTF8 ("\xE2\x80\x94"), r.reduced (6, 3), juce::Justification::topRight, false);
            return;
        }
        const auto& b = *buf_;
        const juce::int64 n = (juce::int64) b.size();
        const juce::int64 half = juce::jmax<juce::int64> (2, (juce::int64) (windowMs_ * 0.0005 * rate_));
        auto sampleAt = [&] (juce::int64 i) -> float { return (i >= 0 && i < n) ? b[(size_t) i] : 0.0f; };
        juce::Path pl, pr;
        for (int x = 0; x < cx; ++x)
        {
            const juce::int64 i = loopEnd_ - half + (juce::int64) std::llround ((double) x / (double) (cx - 1) * (double) half);
            const float y = mid - sampleAt (i) * amp;
            if (x == 0) pl.startNewSubPath ((float) x, y); else pl.lineTo ((float) x, y);
        }
        for (int x = cx; x < w; ++x)
        {
            const juce::int64 i = loopStart_ + (juce::int64) std::llround ((double) (x - cx) / (double) juce::jmax (1, w - 1 - cx) * (double) half);
            const float y = mid - sampleAt (i) * amp;
            if (x == cx) pr.startNewSubPath ((float) x, y); else pr.lineTo ((float) x, y);
        }
        g.setColour (col::mEnd.withAlpha (0.9f));   g.strokePath (pl, juce::PathStrokeType (1.2f));
        g.setColour (col::mStart.withAlpha (0.9f)); g.strokePath (pr, juce::PathStrokeType (1.2f));
        const float jump = std::fabs (sampleAt (loopEnd_) - sampleAt (loopStart_));
        g.setFont (mono (10.0f)); g.setColour (col::text2);
        g.drawText ("step " + juce::String (jump, 3), r.reduced (6, 3), juce::Justification::topRight, false);
        if (playhead_ >= 0.0)
        {
            float px = -1.0f;
            if (playhead_ <= (double) loopEnd_ && playhead_ > (double) (loopEnd_ - half))
                px = (float) cx - (float) (((double) loopEnd_ - playhead_) / (double) half * cx);
            else if (playhead_ >= (double) loopStart_ && playhead_ < (double) (loopStart_ + half))
                px = (float) cx + (float) ((playhead_ - (double) loopStart_) / (double) half * (w - cx));
            if (px >= 0.0f) { g.setColour (col::playhead); g.fillRect (juce::Rectangle<float> (px - 1.0f, 16.0f, 2.0f, (float) h - 16.0f)); }
        }
    }
private:
    DisplayBuffer buf_;
    double rate_ = 44100.0, windowMs_ = 20.0, playhead_ = -1.0;
    juce::int64 loopStart_ = 0, loopEnd_ = 0;
};

}
