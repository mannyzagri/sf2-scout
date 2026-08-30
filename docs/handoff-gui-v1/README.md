# Handoff: SF2 Scout — auditioner GUI

## Overview
Single-window GUI for "SF2 Scout", a minimal VST3 / standalone SF2 auditioner (JUCE 8 / C++17 / CMake,
TinySoundFont for parsing). The tool loads an .sf2, plays its samples from MIDI, and — its real purpose —
displays exactly which zone/sample fired, how far it is being pitch-stretched, and where its loop points
sit. No export, no record, no save (see the NON-features section of the bundled spec).

This handoff covers the **UI layer only**. The audio/parsing behaviour is specified in the bundled
`SF2_AUDITIONER_SPEC.md`, which remains the source of truth for anything not visual.

## About the Design Files
The files in this bundle are **design references created in HTML** — a prototype showing intended look
and behaviour, not production code to copy. The target implementation is **plain JUCE Components**
(per the spec: no WebView, no extra editor framework). Recreate the layout, proportions, hierarchy and
interaction model below using stock JUCE widgets and custom `paint()` for the two bespoke displays
(sample/loop bar, zone map strip).

## Fidelity
**High-fidelity.** Exact hex values, type sizes, and pixel geometry are given below. The intent is a
plain, high-legibility bench-tool look — flat panels, hairline separators, one accent colour, monospace
for every number. It is not meant to look styled; it is meant to be read at a glance while playing.

## Screens / Views

Single window. Design width **920 px**, height ~**700 px** as drawn (spec suggested ~700x420; the design
is larger because the readout and zone map need the room — scale down proportionally if a smaller default
window is required, but do not shrink the zone strip below 48 px tall or the readout numerals below 16 px).

Window shell: background `#f6f6f3`, 1 px border `#c9c9c2`, corner radius 6 px (radius is browser
chrome only — a JUCE plugin window is square; drop it).

### 1. Header bar
Height ~48 px, background `#efefea`, 1 px bottom border `#d7d7d0`, padding 14 px 16 px, horizontal
flex, gap 14 px.
- **LOAD button** — 8 px / 16 px padding, background `#2e5c8a` (hover `#24486d`), white text,
  IBM Plex Sans 500 12 px, letter-spacing .08em, radius 4 px. Opens native file browser. The whole
  window is also a drop target for .sf2 files.
- **Filename block** — label "SOUNDFONT", IBM Plex Sans 400 10 px, letter-spacing .1em, `#8a8a80`;
  value in IBM Plex Mono 500 13 px `#1c1c1a`, ellipsised. Placeholder value in the mock:
  `Vintage_Dreams_Waves_v2.sf2`. Empty state: `— no file loaded —`.
- **Mode switch** (right aligned) — label "MODE" (same small-label style), then a 2-segment control:
  track `#e2e2db`, 1 px border `#d0d0c8`, radius 4 px, 2 px padding, 2 px gap. Segments
  `AS-AUTHORED` and `LOOP-ONLY`, IBM Plex Mono 500 11 px, padding 7 px 12 px, radius 3 px.
  Selected: background `#2e5c8a`, text `#fff`. Unselected: transparent, text `#5c5c54`.
  Default AS-AUTHORED.

### 2. Body — 2 columns
CSS grid `300px 1fr` with a 1 px `#d7d7d0` divider.

#### 2a. Preset list (left, 300 px)
- Header row, padding 10 px 12 px, bottom border `#e3e3dc`: label "PRESETS" + two stepper buttons
  (‹ ›), each 24x22 px, white, 1 px `#d0d0c8`, radius 3 px, hover `#eef2f7`. Prev/Next step the
  selection for fast audition passes.
- Scroll area height 268 px. Rows: padding 9 px 12 px, bottom border `#edede6`, gap 10 px between
  the bank:program id (IBM Plex Mono 400 11 px, `#a3a399`, or accent `#2e5c8a` when selected) and
  the name (IBM Plex Sans 400 13 px `#1c1c1a`). Selected row background `#dfe8f1`.
- Source: SF2 `phdr` records, formatted `bank:program` zero-padded to 3 digits each.

#### 2b. Info readout (right)
Padding 12 px 16 px 14 px, vertical flex, gap 12 px.
- Row of small labels: "LAST NOTE PLAYED" (left) and the active preset `bank:program  Name` in
  IBM Plex Mono 400 11 px `#8a8a80` (right).
- **Four big fields**, baseline-aligned, gap 22 px. Each: 10 px small label above, value in
  IBM Plex Mono 500 20 px.
  - SAMPLE — `shdr` sample name of the zone that fired.
  - PLAYED — played note as name+octave, coloured accent `#2e5c8a`.
  - ROOT — `overridingRootKey` else sample header `originalKey`, as note name.
  - STRETCH — signed semitone difference (played − root), suffix " st". Text `#1c1c1a`, but
    `#d9772b` when |semitones| > 7 (a cheap "this sample is being stretched hard" warning).
- **Stats grid** — 3 columns, gap 8 px / 18 px, padding 10 px 12 px, background `#fff`,
  1 px `#e3e3dc`, radius 4 px. Each cell: key left in `#8a8a80`, value right in `#1c1c1a`,
  both IBM Plex Mono 400 12 px. Nine cells in this order:
  `zone` (lokey–hikey as note names), `vel` (velocity range or 1-127), `rate` (Hz),
  `loopStart` (samples), `loopEnd` (samples), `loop len` (ms; "one-shot" if no loop),
  `length` (sample length, ms), `channels` (mono/stereo), `playback`
  ("from start" in AS-AUTHORED, "from loopStart" in LOOP-ONLY).
  Millisecond formatting: 1 decimal below 100 ms, 0 decimals at or above.
- **Sample / loop bar** — small labels above: "SAMPLE / LOOP REGION" left, "<pct> OF SAMPLE" right
  (or "NO LOOP"). Bar: 34 px tall, background `#e6e6df`, 1 px `#d7d7d0`, radius 3 px, clipped.
  Loop region: absolutely positioned by `loopStart/length` and `(loopEnd-loopStart)/length`,
  fill `#c7d9ea`, 2 px left and right borders `#2e5c8a`. Playhead: 2 px `#d9772b` vertical line
  sweeping the loop region, 2.4 s linear loop in the mock — in the plugin it should track the real
  read pointer. Caption inside the bar at left, IBM Plex Mono 500 11 px `#5c5c54`: "attack" in
  AS-AUTHORED, "attack skipped → starts at N%" in LOOP-ONLY, "plays through once, no loop" for
  unlooped zones.

### 3. Zone map strip
Padding 12 px 16 px 6 px, background `#f1f1ec`, 1 px top border `#d7d7d0`.
- Small labels: "ZONE MAP — CLICK A KEY TO AUDITION" left, "<n> ZONES" right.
- Strip: 128 equal-width cells in a row, 52 px tall, 1 px border `#c9c9c2`, radius 3 px, white base.
  - Cell fill alternates by zone index: `#cfdcea` / `#e7edf4` — so adjacent zones read as distinct
    bands and the boundaries are visible at a glance.
  - Selected / last-played key: fill accent `#2e5c8a`.
  - Black keys: overlay across the top 58 % of the cell, `rgba(28,28,26,.30)` (`rgba(0,0,0,.35)`
    when the key is selected).
  - Zone right edge (`n === zone.hi`): 1 px right border `#7d8a97`.
  - Every C (`n % 12 === 0`): 4 px bottom tick `rgba(28,28,26,.35)` as an octave ruler.
  - Tooltip per cell: "<note>  —  <sampleName>". Click = audition that note.
- **Zone labels row** beneath, 4 px gap: one block per zone, flex-grown by key count
  (`hi - lo + 1`), 1 px left border `#d0d0c8`, 5 px left padding. Line 1: sample name,
  IBM Plex Mono 400 10 px `#5c5c54`, ellipsised. Line 2: "<lo>–<hi>  root <root>",
  IBM Plex Mono 400 10 px `#a3a399`.

### 4. Footer
Padding 12 px 16 px, background `#efefea`, 1 px top border `#d7d7d0`, horizontal flex, gap 16 px.
- "MASTER" small label; slider 200x6 px, track `#dcdcd4` radius 3 px, fill `#2e5c8a`,
  thumb 14x16 px white with 1 px `#b6b6ae` border, radius 3 px. Click/drag to set.
  Readout right of it: IBM Plex Mono 500 12 px, width 56 px, value `20*log10(gain)` to 1 decimal
  + " dB" (0.0 dB at unity). Mock default 0.72 gain.
- Right side: "MIDI IN" small label + a click-to-cycle chip (white, 1 px `#d0d0c8`, radius 4 px,
  padding 6 px 12 px, min-width 64 px, hover `#eef2f7`, IBM Plex Mono 500 12 px) cycling
  OMNI / CH 1..4 — in the plugin this is a ComboBox listing OMNI + channels 1-16, OMNI default.
- Voice counter: 8 px dot (`#4c9a5e` active, `#c2c2ba` idle) + "N/32 voices",
  IBM Plex Mono 400 12 px `#5c5c54`.

## Interactions & Behavior
- Click a preset row, or ‹ / › — selects that preset, reloads the zone table, redraws the strip and
  zone labels. Selection wraps at both ends on the steppers.
- Mode switch — immediate; changes the `playback` stat and the bar caption, and (in the plugin)
  the read-pointer start position for subsequently triggered voices.
- Click a key on the zone strip — triggers that note (default velocity ~100), updates every readout
  field, and highlights the cell.
- MIDI note-on — same update path as a strip click. The readout always reflects the MOST RECENT
  note only; no history.
- MIDI chip — cycles the channel filter in the mock; a dropdown in the plugin.
- Volume — click/drag along the track; clamp 0..1.
- Hover states are limited to the three button-like surfaces (LOAD, steppers, MIDI chip).
- Loading a malformed file: show an error message in the filename slot / an alert, keep the previous
  state, never crash (spec §1).
- Empty state (nothing loaded): preset list empty, readout fields "—", strip drawn in a single
  neutral shade, zone label row empty.
- No responsive behaviour needed — fixed-size plugin window. If resizing is allowed, the preset
  column stays 300 px and the readout column absorbs the difference; the zone strip stretches.

## State Management
- `presetIndex` (int) — selected `phdr` entry.
- `lastNote` (0-127) — most recent note-on; drives the entire readout.
- `mode` — enum { AS_AUTHORED, LOOP_ONLY }, global, default AS_AUTHORED.
- `masterGain` (0..1, default 0.72) — apply on the audio thread, smoothed.
- `midiChannelFilter` — 0 = OMNI, else 1-16.
- `activeVoiceCount` (0..32) — polled for the footer counter; do not push from the audio thread.
- Derived per note-on, on the message thread from the parsed zone table: zone/sample record,
  root key, loop points, sample length, rate, channel count, semitone offset. The zone table
  (`phdr/pbag/inst/ibag/shdr` flattened) is built once at load, as the spec's build order says.
- Playhead position: read atomically from the active voice; if that is inconvenient, drive it from
  the loop length as a free-running animation (the mock does this) — it is a display aid, not a meter.

## Design Tokens
Colors
- Window / panel: `#f6f6f3`  · Header & footer: `#efefea`  · Zone map band: `#f1f1ec`
- Page behind window: `#e9e9e5`  · Inset panels: `#ffffff`
- Borders: `#c9c9c2` (outer), `#d7d7d0` (section), `#e3e3dc` / `#edede6` (hairline row),
  `#d0d0c8` (control), `#b6b6ae` (thumb), `#7d8a97` (zone edge)
- Text: `#1c1c1a` primary · `#5c5c54` secondary · `#8a8a80` label · `#a3a399` tertiary
- Accent: `#2e5c8a` (hover `#24486d`) · selected row `#dfe8f1` · loop fill `#c7d9ea`
- Zone bands: `#cfdcea` / `#e7edf4`
- Warning / playhead: `#d9772b` · OK dot: `#4c9a5e` · idle dot: `#c2c2ba`
- Slider track: `#dcdcd4` · switch track: `#e2e2db` · bar base: `#e6e6df`

Typography — IBM Plex Sans (UI) + IBM Plex Mono (all numbers, names, values)
- Small label: Sans 400 10 px, letter-spacing .1em, uppercase, `#8a8a80`
- Button: Sans 500 12 px, letter-spacing .08em
- Body / list name: Sans 400 13 px
- Mono small: 10-11 px 400 · Mono value: 12-13 px 400/500 · Mono headline: 20 px 500
- Substitute: if IBM Plex is not bundled, use the JUCE default sans + any bundled monospace; keep
  the sans/mono split, it carries most of the legibility.

Spacing: 2, 4, 6, 8, 10, 12, 14, 16, 18, 22 px. Radii: 3 px (inner), 4 px (control), 6 px (window).
Shadow: window only, `0 12px 32px rgba(20,20,18,.14)` — drop in the plugin.

## Assets
None. No images, no icons, no SVG — the only glyphs are the ‹ › chevrons (U+2039 / U+203A) and
the em/en dashes in labels. Fonts are Google-hosted IBM Plex in the prototype; bundle them or
substitute as above.

## Files
- `SF2 Scout GUI.dc.html` — the interactive prototype. Open in a browser. All layout is inline
  styles in the template; all state, formatting and the mock zone tables are in the logic class at
  the bottom of the file (see `PRESETS`, `renderVals()`), which is a readable reference for the
  exact derived-value formatting.
- `SF2_AUDITIONER_SPEC.md` — the original functional spec (scope, play modes, release behaviour,
  what to ignore from the SF2 spec, non-features, acceptance tests, build order). Authoritative for
  behaviour; this README is authoritative for appearance.

## Note on the mock data
All preset names, sample names, key ranges and loop points in the prototype are invented placeholder
content used to exercise the layout (including "Church Organ" as the no-loop / one-shot case). They
are not from any real SoundFont.
