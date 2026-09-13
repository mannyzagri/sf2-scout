// IBM Plex Sans / Mono from BinaryData (third_party/fonts, OFL 1.1) -- bundled
// per GUI handoff v2 (retires deviation D-2). Sizes in the handoff are CSS px
// = the font's em size, so fonts are built with withPointHeight(px); the
// letter-spacing the handoff gives in px is applied as JUCE's kerning factor
// (a proportion of the JUCE height, hence the division).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace sf2scout::ui::fonts
{
enum class Weight { Regular, Medium, SemiBold, Bold };

struct Set
{
    juce::Typeface::Ptr sansRegular, sansMedium, sansSemiBold, monoRegular, monoMedium, monoBold;
};

inline const Set& set()
{
    static const Set s = []
    {
        Set t;
        t.sansRegular  = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexSansRegular_ttf,  ScoutFonts::IBMPlexSansRegular_ttfSize);
        t.sansMedium   = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexSansMedium_ttf,   ScoutFonts::IBMPlexSansMedium_ttfSize);
        t.sansSemiBold = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexSansSemiBold_ttf, ScoutFonts::IBMPlexSansSemiBold_ttfSize);
        t.monoRegular  = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexMonoRegular_ttf,  ScoutFonts::IBMPlexMonoRegular_ttfSize);
        t.monoMedium   = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexMonoMedium_ttf,   ScoutFonts::IBMPlexMonoMedium_ttfSize);
        t.monoBold     = juce::Typeface::createSystemTypefaceFor (ScoutFonts::IBMPlexMonoBold_ttf,     ScoutFonts::IBMPlexMonoBold_ttfSize);
        return t;
    }();
    return s;
}

inline juce::Font make (juce::Typeface::Ptr tf, float px, float spacingPx, bool monoFallback, bool boldFallback)
{
    juce::Font f = tf != nullptr
        ? juce::Font (juce::FontOptions (tf).withPointHeight (px))
        : juce::Font (juce::FontOptions (monoFallback ? juce::Font::getDefaultMonospacedFontName() : juce::Font::getDefaultSansSerifFontName(),
                                         px * 1.25f, boldFallback ? juce::Font::bold : juce::Font::plain));
    if (spacingPx != 0.0f && f.getHeight() > 0.0f) f.setExtraKerningFactor (spacingPx / f.getHeight());
    return f;
}

inline juce::Font sans (float px, Weight w = Weight::Regular, float spacingPx = 0.0f)
{
    const Set& s = set();
    juce::Typeface::Ptr tf = w == Weight::Regular ? s.sansRegular : w == Weight::Medium ? s.sansMedium : s.sansSemiBold;   // Bold -> SemiBold (600)
    return make (tf, px, spacingPx, false, w != Weight::Regular);
}

inline juce::Font mono (float px, Weight w = Weight::Regular, float spacingPx = 0.0f)
{
    const Set& s = set();
    juce::Typeface::Ptr tf = w == Weight::Regular ? s.monoRegular : w == Weight::Medium ? s.monoMedium : s.monoBold;       // SemiBold -> Bold (700)
    return make (tf, px, spacingPx, true, w != Weight::Regular);
}
}
