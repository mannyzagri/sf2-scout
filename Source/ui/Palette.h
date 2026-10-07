// Design tokens -- verbatim from docs/handoff-gui-v2/README.md "Design tokens"
// (unchanged v1 set + the v2 additions). Fonts: IBM Plex Sans / Mono bundled
// (BinaryData, see Fonts.h) -- deviation D-2 retired by handoff v2.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Fonts.h"

namespace sf2scout::ui
{
inline juce::Colour hex (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }

namespace col
{
    inline const juce::Colour window     = hex (0xf6f6f3);
    inline const juce::Colour headFoot   = hex (0xefefea);
    inline const juce::Colour zoneBand   = hex (0xf1f1ec);
    inline const juce::Colour inset      = hex (0xffffff);
    inline const juce::Colour borderOut  = hex (0xc9c9c2);
    inline const juce::Colour borderSec  = hex (0xd7d7d0);
    inline const juce::Colour hairline   = hex (0xe3e3dc);
    inline const juce::Colour hairlineRow= hex (0xedede6);
    inline const juce::Colour control    = hex (0xd0d0c8);
    inline const juce::Colour thumb      = hex (0xb6b6ae);
    inline const juce::Colour zoneEdge   = hex (0x7d8a97);
    inline const juce::Colour text       = hex (0x1c1c1a);
    inline const juce::Colour text2      = hex (0x5c5c54);
    inline const juce::Colour label      = hex (0x8a8a80);
    inline const juce::Colour text3      = hex (0xa3a399);
    inline const juce::Colour accent     = hex (0x2e5c8a);
    inline const juce::Colour accentHov  = hex (0x24486d);
    inline const juce::Colour selRow     = hex (0xdfe8f1);
    inline const juce::Colour loopFill   = hex (0xc7d9ea);
    inline const juce::Colour zoneA      = hex (0xcfdcea);
    inline const juce::Colour zoneB      = hex (0xe7edf4);
    inline const juce::Colour warn       = hex (0xd9772b);
    inline const juce::Colour okDot      = hex (0x4c9a5e);
    inline const juce::Colour idleDot    = hex (0xc2c2ba);
    inline const juce::Colour sliderTrack= hex (0xdcdcd4);
    inline const juce::Colour switchTrack= hex (0xe2e2db);
    inline const juce::Colour barBase    = hex (0xe6e6df);
    inline const juce::Colour hoverBtn   = hex (0xeef2f7);
    // v2 additions
    inline const juce::Colour mStart     = hex (0x4c9a5e);
    inline const juce::Colour mEnd       = hex (0xc2453a);
    inline const juce::Colour wave       = hex (0x2e5c8a);
    inline const juce::Colour playhead   = hex (0xd9772b);
    inline const juce::Colour badgeSf2   = accent;
    inline const juce::Colour badgeMod   = okDot;
    inline const juce::Colour badgeWav   = warn;
    inline const juce::Colour badgeErr   = mEnd;
    inline const juce::Colour periodsWarnBg = hex (0xfbeee2);
    inline const juce::Colour clickYellow = hex (0xd9b52b);
    inline const juce::Colour slotA      = accent;
    inline const juce::Colour slotB      = text2;
}

// ---- type scale (README "Type scale (Plex)")
inline juce::Font smallLabel()  { return fonts::sans (10.0f, fonts::Weight::Medium, 1.0f); }     // uppercase, colour label
inline juce::Font buttonFont()  { return fonts::sans (11.0f, fonts::Weight::Bold, 0.9f); }       // uppercase
inline juce::Font listHeader()  { return fonts::sans (9.0f, fonts::Weight::Medium, 0.8f); }      // text3
inline juce::Font nameFont (bool medium = false) { return fonts::sans (12.0f, medium ? fonts::Weight::Medium : fonts::Weight::Regular); }
inline juce::Font sans (float px, fonts::Weight w = fonts::Weight::Regular) { return fonts::sans (px, w); }
inline juce::Font mono (float px, bool bold = false) { return fonts::mono (px, bold ? fonts::Weight::Bold : fonts::Weight::Regular); }
inline juce::Font badgeFont()   { return fonts::mono (9.0f, fonts::Weight::Bold, 0.5f); }
inline juce::Font segFont (float px = 11.0f) { return fonts::mono (px, fonts::Weight::Bold); }
}
