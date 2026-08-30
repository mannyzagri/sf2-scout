// Design tokens -- verbatim from docs/handoff-gui-v1/README.md "Design Tokens".
// Fonts: IBM Plex is not bundled (recorded deviation D-2); the handoff's own
// substitution rule applies -- JUCE default sans + a monospace, sans/mono split kept.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

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
}

inline juce::Font sans (float px, bool medium = false)
{
    return juce::Font (juce::FontOptions (px, medium ? juce::Font::bold : juce::Font::plain));
}
inline juce::Font mono (float px, bool medium = false)
{
    return juce::Font (juce::FontOptions ("Consolas", px, medium ? juce::Font::bold : juce::Font::plain));
}
// "Small label: Sans 400 10 px, letter-spacing .1em, uppercase, #8a8a80"
inline juce::Font smallLabel()
{
    juce::Font f = sans (10.0f);
    f.setExtraKerningFactor (0.1f);
    return f;
}
inline juce::Font buttonFont()
{
    juce::Font f = sans (12.0f, true);
    f.setExtraKerningFactor (0.08f);
    return f;
}
}
