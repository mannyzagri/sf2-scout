// The info readout, the sample/loop bar and the zone map -- the three bespoke
// displays of docs/handoff-gui-v1/README.md §2b and §3, drawn with paint().
#pragma once
#include "Palette.h"
#include "../engine/SoundFontBank.h"
#include "../engine/ScoutEngine.h"
#include "../engine/NoteNames.h"
#include <functional>
#include <optional>

namespace sf2scout::ui
{

// What every display is fed: the last note + the zone it landed on.
// F3: no raw pointers into a bank the processor's 10 Hz collector can delete --
// everything here is a value copied on the message thread at update() time.
struct ReadoutState
{
    int presetIndex = -1;
    int note = -1;                        // -1 = nothing played yet
    std::optional<Zone> zone;             // copy of the zone described (nullopt = none)
    PlayMode mode = PlayMode::AsAuthored;
    double playhead = -1.0;               // sample-relative, -1 = stopped
    juce::String presetLabel;             // precomputed "bank:program  Name" (or a dash), for the row header

    // Slot W column (docs/SF2SCOUT_WAV_EXTENSION.md "two-column when both loaded")
    struct WavInfo
    {
        juce::String file;
        int root = 60; double cents = 0.0;
        juce::int64 loopStart = 0, loopEnd = 0;   // inclusive
        int mode = 0;                             // 0 fwd, 1 ping-pong, 2 off
        juce::int64 frames = 0; double rate = 44100.0;
        int lastNote = -1; bool stereo = false; int bits = 16; bool isFloat = false;
    };
    std::optional<WavInfo> wav;
};

// frequency of a MIDI note (+ cents) with A4 = 440
inline double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

inline juce::String S (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
inline juce::String dash() { return juce::String::fromUTF8 ("\xE2\x80\x94"); }
inline juce::String enDash() { return juce::String::fromUTF8 ("\xE2\x80\x93"); }

// ----------------------------------------------------------------- readout
class InfoReadout : public juce::Component
{
public:
    void update (const ReadoutState& s) { st_ = s; repaint(); }

    // heights used by the editor layout
    static constexpr int kPadTop = 12, kPadX = 16, kPadBottom = 14, kGap = 12;
    static constexpr int kLabelRow = 12, kBigRow = 38, kStatsH = 82;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced (kPadX, 0).withTrimmedTop (kPadTop).withTrimmedBottom (kPadBottom);
        if (st_.wav)
        {
            auto wArea = area.removeFromRight (kWavColW);
            area.removeFromRight (kColGap);
            g.setColour (col::hairline); g.fillRect (area.getRight() + kColGap / 2, area.getY(), 1, area.getHeight());
            paintWavColumn (g, wArea);
        }
        const Zone* z = st_.zone ? &(*st_.zone) : nullptr;
        const bool have = st_.note >= 0 && z != nullptr;

        // row 1: LAST NOTE PLAYED ...... bank:program  Name
        auto row = area.removeFromTop (kLabelRow);
        g.setFont (smallLabel()); g.setColour (col::label);
        g.drawText ("LAST NOTE PLAYED", row, juce::Justification::centredLeft, false);
        g.setFont (mono (11.0f));
        g.drawText (st_.presetLabel, row, juce::Justification::centredRight, false);
        area.removeFromTop (kGap);

        // row 2: four big fields
        auto big = area.removeFromTop (kBigRow);
        const int semis = have ? st_.note - z->rootKey : 0;
        struct Field { juce::String label, value; juce::Colour colour; };
        const Field fields[4] = {
            { "SAMPLE",  have ? S (z->sampleName) : dash(), col::text },
            { "PLAYED",  st_.note >= 0 ? S (noteName (st_.note)) : dash(), col::accent },
            { "ROOT",    have ? S (noteName (z->rootKey)) : dash(), col::text },
            { "STRETCH", have ? juce::String (semis > 0 ? "+" : "") + juce::String (semis) + " st" : dash(),
                         std::abs (semis) > 7 ? col::warn : col::text },
        };
        int x = big.getX();
        for (const auto& f : fields)
        {
            const int w = juce::jmax ((int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (20.0f, true), f.value)),
                                      (int) std::ceil (juce::GlyphArrangement::getStringWidth (smallLabel(), f.label)));
            g.setFont (smallLabel()); g.setColour (col::label);
            g.drawText (f.label, x, big.getY(), w + 4, 12, juce::Justification::centredLeft, false);
            g.setFont (mono (20.0f, true)); g.setColour (f.colour);
            g.drawText (f.value, x, big.getY() + 15, w + 4, 23, juce::Justification::centredLeft, false);
            x += w + 22;
        }
        area.removeFromTop (kGap);

        // row 3: stats grid 3 cols, white inset, 1px #e3e3dc, r4
        auto grid = area.removeFromTop (kStatsH);
        g.setColour (col::inset);    g.fillRoundedRectangle (grid.toFloat(), 4.0f);
        g.setColour (col::hairline); g.drawRoundedRectangle (grid.toFloat().reduced (0.5f), 4.0f, 1.0f);
        auto inner = grid.reduced (12, 10);
        const int colW = (inner.getWidth() - 2 * 18) / 3;
        const int rowH = (inner.getHeight() - 2 * 8) / 3;
        juce::String vals[9];
        const juce::String keys[9] = { "zone", "vel", "rate", "loopStart", "loopEnd", "loop len", "length", "channels", "playback" };
        if (have)
        {
            const double rate = (double) z->sampleRate;
            const bool hasLoop = z->hasLoop();
            vals[0] = S (noteName (z->lokey)) + enDash() + S (noteName (z->hikey));
            vals[1] = juce::String (juce::jmax (1, z->lovel)) + "-" + juce::String (z->hivel);
            vals[2] = juce::String (z->sampleRate) + " Hz";
            vals[3] = hasLoop ? juce::String ((int) z->loopStartRel()) : dash();
            vals[4] = hasLoop ? juce::String ((int) z->loopEndRel()) : dash();
            vals[5] = hasLoop ? S (formatMs (samplesToMs (z->loopLength(), rate))) : "one-shot";
            vals[6] = S (formatMs (samplesToMs (z->sampleLength(), rate)));
            vals[7] = z->isStereoHalf() ? "stereo" : "mono";
        }
        else
            for (auto& v : vals) v = dash();
        vals[8] = st_.mode == PlayMode::LoopOnly ? "from loopStart" : "from start";
        g.setFont (mono (12.0f));
        for (int i = 0; i < 9; ++i)
        {
            juce::Rectangle<int> cell (inner.getX() + (i % 3) * (colW + 18), inner.getY() + (i / 3) * (rowH + 8), colW, rowH);
            g.setColour (col::label); g.drawText (keys[i], cell, juce::Justification::centredLeft, false);
            g.setColour (col::text);  g.drawText (vals[i], cell, juce::Justification::centredRight, false);
        }
        area.removeFromTop (kGap);

        // row 4: sample / loop bar
        auto labels = area.removeFromTop (12);
        g.setFont (smallLabel()); g.setColour (col::label);
        g.drawText ("SAMPLE / LOOP REGION", labels, juce::Justification::centredLeft, false);
        juce::String pct = have && z->hasLoop()
            ? juce::String (juce::roundToInt (100.0 * z->loopLength() / juce::jmax (1u, z->sampleLength()))) + "% OF SAMPLE"
            : (have ? "NO LOOP" : dash() + " OF SAMPLE");
        g.drawText (pct, labels, juce::Justification::centredRight, false);
        area.removeFromTop (5);
        auto bar = area.removeFromTop (34);
        paintBar (g, bar, have, z);
    }

    static juce::String presetId (const Preset& p)
    {
        return juce::String (p.bank).paddedLeft ('0', 3) + ":" + juce::String (p.program).paddedLeft ('0', 3);
    }

    static constexpr int kWavColW = 232, kColGap = 18;

private:
    void paintWavColumn (juce::Graphics& g, juce::Rectangle<int> a)
    {
        const auto& w = *st_.wav;
        auto row = a.removeFromTop (kLabelRow);
        g.setFont (smallLabel()); g.setColour (col::label);
        g.drawText ("SLOT W " + dash() + " WORK WAV", row, juce::Justification::centredLeft, false);
        a.removeFromTop (kGap);
        auto big = a.removeFromTop (kBigRow);
        const int semis = w.lastNote >= 0 ? w.lastNote - w.root : 0;
        struct Field { juce::String label, value; juce::Colour colour; };
        const Field fields[3] = {
            { "PLAYED",  w.lastNote >= 0 ? S (noteName (w.lastNote)) : dash(), col::accent },
            { "ROOT",    S (noteName (w.root)) + (std::fabs (w.cents) > 0.05 ? juce::String::formatted (" %+.0fc", w.cents) : juce::String()), col::text },
            { "STRETCH", w.lastNote >= 0 ? juce::String (semis > 0 ? "+" : "") + juce::String (semis) + " st" : dash(), std::abs (semis) > 7 ? col::warn : col::text },
        };
        int x = big.getX();
        for (const auto& f : fields)
        {
            const int tw = juce::jmax ((int) std::ceil (juce::GlyphArrangement::getStringWidth (mono (20.0f, true), f.value)),
                                       (int) std::ceil (juce::GlyphArrangement::getStringWidth (smallLabel(), f.label)));
            g.setFont (smallLabel()); g.setColour (col::label);
            g.drawText (f.label, x, big.getY(), tw + 4, 12, juce::Justification::centredLeft, false);
            g.setFont (mono (20.0f, true)); g.setColour (f.colour);
            g.drawText (f.value, x, big.getY() + 15, tw + 4, 23, juce::Justification::centredLeft, false);
            x += tw + 18;
        }
        a.removeFromTop (kGap);
        // key/value list (samples first, ms + periods derived -- LOOP_BENCH_SPEC §5)
        const juce::int64 len = w.loopEnd >= w.loopStart ? w.loopEnd - w.loopStart + 1 : 0;
        const double period = w.rate / noteHz (w.root + w.cents / 100.0);
        const double periods = period > 0.0 ? (double) len / period : 0.0;
        const bool nearInt = std::fabs (periods - std::round (periods)) < 0.1;
        const char* modeName[3] = { "forward", "ping-pong", "off (one-shot)" };
        struct KV { juce::String k, v; juce::Colour c; };
        const KV rows[] = {
            { "file",       w.file, col::text },
            { "loopStart",  juce::String (w.loopStart) + "  " + S (formatMs (samplesToMs ((double) w.loopStart, w.rate))), col::text },
            { "loopEnd",    juce::String (w.loopEnd) + "  " + S (formatMs (samplesToMs ((double) w.loopEnd, w.rate))), col::text },
            { "loop len",   juce::String (len) + "  " + S (formatMs (samplesToMs ((double) len, w.rate))) + "  " + juce::String (periods, 2) + " per", nearInt ? col::text : col::warn },
            { "loop type",  modeName[juce::jlimit (0, 2, w.mode)], col::text },
            { "length",     juce::String (w.frames) + "  " + S (formatMs (samplesToMs ((double) w.frames, w.rate))), col::text },
            { "format",     juce::String ((int) w.rate) + " Hz  " + juce::String (w.bits) + (w.isFloat ? "f  " : "-bit  ") + (w.stereo ? "stereo" : "mono"), col::text },
        };
        g.setFont (mono (11.0f));
        for (const auto& kv : rows)
        {
            auto line = a.removeFromTop (17);
            g.setColour (col::label); g.drawText (kv.k, line.removeFromLeft (62), juce::Justification::centredLeft, false);
            g.setColour (kv.c);       g.drawText (kv.v, line, juce::Justification::centredLeft, true);
        }
    }

    void paintBar (juce::Graphics& g, juce::Rectangle<int> bar, bool have, const Zone* z)
    {
        g.setColour (col::barBase);  g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        juce::String caption;
        if (have)
        {
            const double len = juce::jmax (1u, z->sampleLength());
            const bool hasLoop = z->hasLoop();
            const double l0 = hasLoop ? z->loopStartRel() / len : 0.0;
            const double l1 = hasLoop ? z->loopEndRel() / len : 1.0;
            juce::Rectangle<float> loopR ((float) bar.getX() + (float) (bar.getWidth() * l0), (float) bar.getY(),
                                          (float) (bar.getWidth() * (l1 - l0)), (float) bar.getHeight());
            if (hasLoop || st_.mode == PlayMode::LoopOnly)
            {
                g.setColour (col::loopFill); g.fillRect (loopR);
                g.setColour (col::accent);
                g.fillRect (loopR.withWidth (2.0f));
                g.fillRect (loopR.withLeft (loopR.getRight() - 2.0f));
            }
            if (st_.playhead >= 0.0)
            {
                const float px = (float) bar.getX() + (float) (bar.getWidth() * juce::jlimit (0.0, 1.0, st_.playhead / len));
                g.setColour (col::warn);
                g.fillRect (juce::Rectangle<float> (px - 1.0f, (float) bar.getY(), 2.0f, (float) bar.getHeight()));
            }
            if (! hasLoop)                          caption = "plays through once, no loop";
            else if (st_.mode == PlayMode::LoopOnly) caption = juce::String::fromUTF8 ("attack skipped \xE2\x86\x92 starts at ") + juce::String (juce::roundToInt (l0 * 100.0)) + "%";
            else                                     caption = "attack";
            if (! hasLoop && st_.mode == PlayMode::LoopOnly) caption = "no loop: whole sample loops";
        }
        g.setColour (col::borderSec); g.drawRoundedRectangle (bar.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setFont (mono (11.0f, true)); g.setColour (col::text2);
        g.drawText (caption, bar.withTrimmedLeft (8), juce::Justification::centredLeft, false);
    }

    ReadoutState st_;
};

// ----------------------------------------------------------------- zone map
class ZoneMapStrip : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onKeyDown;    // audition
    std::function<void (int)> onKeyUp;

    // F3: copies the preset's zones by value at update time (message thread,
    // bank guaranteed live) instead of keeping a pointer into the bank.
    void update (const SoundFontBank* bank, int presetIndex, int selectedNote)
    {
        zones_.clear();
        if (bank != nullptr && presetIndex >= 0 && presetIndex < bank->presetCount())
            zones_ = bank->presets()[(size_t) presetIndex].zones;
        selected_ = selectedNote;
        buildCoverage();
        repaint();
    }
    int zoneCount() const { return (int) zones_.size(); }
    // Non-owning view into this strip's own copy -- valid as long as the strip is
    // (used by ZoneLabelsRow, which is a sibling with the same editor lifetime).
    const Zone* zoneAt (int i) const { return (i >= 0 && i < (int) zones_.size()) ? &zones_[(size_t) i] : nullptr; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (col::inset); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float cw = (float) r.getWidth() / 128.0f;
        for (int n = 0; n < 128; ++n)
        {
            juce::Rectangle<float> cell ((float) r.getX() + n * cw, (float) r.getY(), cw, (float) r.getHeight());
            const int zi = zoneOfKey_[n];
            const bool sel = n == selected_;
            juce::Colour bg = sel ? col::accent : (zones_.empty() ? col::zoneB : (zi < 0 ? col::inset : ((zi % 2) == 0 ? col::zoneA : col::zoneB)));
            g.setColour (bg); g.fillRect (cell);
            if (isBlackKey (n))
            {
                g.setColour (sel ? juce::Colour (0x59000000) : juce::Colour (0x4d1c1c1a));
                g.fillRect (cell.withHeight (cell.getHeight() * 0.58f));
            }
            if (n % 12 == 0)
            {
                g.setColour (juce::Colour (0x591c1c1a));
                g.fillRect (cell.withTop (cell.getBottom() - 4.0f));
            }
            if (zi >= 0 && zones_[(size_t) zi].hikey == n)
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
        juce::String tip = S (noteName (n)) + "  " + dash() + "  ";
        const int zi = zoneOfKey_[n];
        tip += (zi >= 0) ? S (zones_[(size_t) zi].sampleName) : juce::String ("(no zone)");
        setTooltip (tip);
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
    int zoneOfKey (int n) const { return (n >= 0 && n < 128) ? zoneOfKey_[n] : -1; }

private:
    int keyAt (int x) const { return juce::jlimit (0, 127, (int) (x * 128 / juce::jmax (1, getWidth()))); }
    void buildCoverage()
    {
        for (int n = 0; n < 128; ++n)
        {
            zoneOfKey_[n] = -1;
            for (size_t i = 0; i < zones_.size(); ++i)
                if (zones_[i].coversKey (n)) { zoneOfKey_[n] = (int) i; break; }
        }
    }
    std::vector<Zone> zones_;      // F3: value copy of the current preset's zones
    int selected_ = -1, held_ = -1;
    int zoneOfKey_[128] {};
};

// Zone labels row beneath the strip: one block per zone, width by key count.
class ZoneLabelsRow : public juce::Component
{
public:
    void update (const ZoneMapStrip& strip) { strip_ = &strip; repaint(); }
    void paint (juce::Graphics& g) override
    {
        if (strip_ == nullptr || strip_->zoneCount() == 0) return;
        // Blocks follow the FIRST-covering zone per key so they line up with the strip's bands.
        const float cw = (float) getWidth() / 128.0f;
        int n = 0;
        while (n < 128)
        {
            const int zi = strip_->zoneOfKey (n);
            int end = n;
            while (end + 1 < 128 && strip_->zoneOfKey (end + 1) == zi) ++end;
            if (const Zone* zp = strip_->zoneAt (zi))
            {
                const Zone& z = *zp;
                juce::Rectangle<int> block ((int) (n * cw), 0, (int) ((end - n + 1) * cw), getHeight());
                g.setColour (col::control); g.fillRect (block.getX(), 0, 1, getHeight());
                auto text = block.withTrimmedLeft (6).withTrimmedRight (6);
                g.setFont (mono (10.0f));
                g.setColour (col::text2);
                g.drawText (S (z.sampleName), text.removeFromTop (13), juce::Justification::centredLeft, true);
                g.setColour (col::text3);
                g.drawText (S (noteName (z.lokey)) + enDash() + S (noteName (z.hikey)) + "  root " + S (noteName (z.rootKey)),
                            text.removeFromTop (13), juce::Justification::centredLeft, true);
            }
            n = end + 1;
        }
    }
private:
    const ZoneMapStrip* strip_ = nullptr;
};

}
