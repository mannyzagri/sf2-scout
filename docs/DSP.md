# SF2_AUDITIONER_SPEC.md — "SF2 Scout" reference auditioner (VM Claude task)

> **SUPERSEDED 2026-09-12 by `docs/SCOUT_v2_SPEC.md`** (kept verbatim). The
> "no export / no save" NON-features below no longer apply; SF2 files remain
> read-only. Still canon for SF2-side detail v2 does not restate.

## Purpose (read this first — it scopes everything down)

A small, standalone VST3 (+ standalone app target if trivial) whose ONLY job
is: load an SF2, play its samples from MIDI, and show what's being played.
It is a LISTENING REFERENCE TOOL for a sound-recreation workflow — the user
auditions commercial SF2 sounds, then recreates them by ear on his own
hardware (JD-990, Quasar, Nord Lead 3/4, XL-1) and samples THOSE. Nothing
from the SF2 is ever exported, resampled, or redistributed — so this tool
needs NO export, NO record, NO save. Keep it deliberately minimal.

This is a days-scale task, not a weeks-scale one. Resist scope growth.

## Tech approach

- JUCE 8 / C++17 / CMake, same toolchain as The Dreamer (ENVIRONMENT.md).
- SF2 parsing + voice rendering: use **TinySoundFont** (single-header C,
  MIT license, battle-tested) as the base. It parses the RIFF structure,
  zones, generators, and renders voices. Wrap it; don't reimplement RIFF
  walking. If TSF's rendering model fights the custom play modes below,
  keep TSF for PARSING only and write the simple sample playback yourself
  (it's a read pointer with loop logic — see §Play modes).
- No editor framework beyond stock JUCE components. Function over beauty;
  this is a bench tool. (WebView GUI NOT required — plain JUCE UI is fine
  and faster to build.)

## Core features

### 1. Loading
- Drag-and-drop an .sf2 anywhere onto the window, AND a "LOAD" button with
  a native file browser. Show the SF2's name once loaded.
- Preset list: a simple scrollable list (bank:program + name) from the
  SF2's preset headers (phdr). Click to select. Prev/Next buttons for
  quick stepping during audition sessions.
- Handle malformed files gracefully: refuse with a message, never crash.
  (User will be feeding it random-provenance files.)

### 2. MIDI input
- Respond to note-on/note-off on all channels (omni). Velocity → level
  only (simple linear or sqrt curve; do NOT honor SF2 velocity crossfades
  — see "What to ignore").
- Pitch bend: ±2 semitones, nice-to-have, low priority.
- Polyphony: 32 voices, oldest-note stealing. No sustain pedal needed
  (nice-to-have if free).

### 3. Play modes (the one custom part — get this right)

A 2-way mode switch, global:

- **AS-AUTHORED** (default): honor the file's own playback data per zone:
  start at sample start, play the attack, then loop between loopStart/
  loopEnd per the zone's sampleModes generator (no-loop zones just play
  through once). Pitch from the zone's root key (overridingRootKey else
  the sample header's originalKey) + coarse/fine tune generators +
  scaleTuning, transposed to the played note. This mode answers "what does
  this sound ACTUALLY sound like."
- **LOOP-ONLY**: skip the attack entirely — start playback AT loopStart
  and loop the [loopStart, loopEnd) region from the first sample, like an
  oscillator. Same pitch handling. For zones with no loop defined, fall
  back to looping the whole sample. This mode answers "what raw sustain
  wave is inside this sound" — useful for judging what a recreated patch's
  loop region needs to contain.
- Loop interpolation: linear is fine (this is an audition tool). A ~5 ms
  equal-power crossfade at the loop seam ONLY if raw looping clicks
  audibly; the SF2's own loop points are usually click-free by design.

### 4. Release behavior
- Do NOT implement the SF2 volume envelope. On note-off, apply a fixed
  protective release fade (~80 ms linear/exp) and free the voice. In
  AS-AUTHORED mode for loop-until-release zones, it's acceptable (and
  simpler) to fade from wherever the loop is rather than playing the
  post-loop tail. This keeps the tool honest: the user is auditioning
  SAMPLES, not the SF2's synthesis layer (his hardware replaces that).

### 5. The info display (this is why the tool exists — don't skimp here)

A readout panel, updated per note played, showing for the MOST RECENT note:
- Preset name, bank:program
- **Which zone/sample fired**: sample name (from shdr), its key range
  (lokey-hikey), velocity range if any
- **Root key** (the note the sample was recorded at) and the played note —
  so the user can see how far the sample is being stretched
- **Loop points**: loopStart/loopEnd in samples AND ms, loop length in ms,
  and the loop region as % of the sample
- Sample rate, sample length (ms), mono/stereo
- A simple horizontal bar representing the sample with the loop region
  highlighted and a moving playhead. One bar, the last-played sample only.
  No waveform rendering required (nice-to-have: min/max waveform strip).

Additionally a static **zone map strip**: 128 keys drawn as a piano strip,
colored by which sample covers each key (alternate two shades at each zone
boundary), so the user can SEE the multisample layout — which keys share a
sample, where the zone edges sit. Click a key on the strip = audition that
note. This is the single most valuable display for planning his own
capture session (which notes to sample on hardware to match the layout).

### 6. Minimal controls
- Master volume knob.
- Mode switch (AS-AUTHORED / LOOP-ONLY).
- MIDI channel filter dropdown (OMNI default) — low priority.
- That's it. No filter, no envelope controls, no FX. It's a scout, not a
  synth.

## What to IGNORE from the SF2 spec (deliberate scope cuts)

Ignore ALL of: volume/mod envelopes (except our fixed release fade), LFOs
(vibrato/mod), the filter (initialFilterFc/Q), reverb/chorus send
generators, modulators (the entire mdta modulator machinery), velocity
crossfade layers (if a zone has velRange, pick the zone matching played
velocity; do not crossfade), stereo linked-sample subtleties beyond basic
L/R pairs, 24-bit sm24 extension (16-bit is fine), and ROM samples flags.
Rationale: the tool auditions the recorded SAMPLES; the SF2's synthesis
layer would only mask what the user needs to hear and recreate.

## Explicit NON-features (do not build, even if easy)
- NO audio export, NO sample extraction, NO save-as, NO drag-out of audio.
  The tool must not become a conversion utility — the entire workflow's
  legality rests on samples never leaving the SF2s. Omitting export keeps
  the tool's purpose unambiguous.
- No preset editing, no writing to the SF2.
- No hosting inside The Dreamer — separate plugin, separate repo/folder.

## UI sketch (plain JUCE, one window ~700x420)

  [ LOAD ]  <sf2 filename>                       [MODE: AS-AUTHORED|LOOP]
  +------------------------------+  +--------------------------------+
  | preset list                  |  | INFO READOUT                   |
  | 000:000 Strings Ens 1        |  | sample: StrEns_C4  rate 44100  |
  | 000:001 Warm Pad             |  | zone C3-B4  root C4  played E4 |
  | ...                          |  | loop 88200..176400 (2000ms 45%)|
  |                              |  | [====|LOOP========|--] >play   |
  +------------------------------+  +--------------------------------+
  [ zone map piano strip — 128 keys, colored by sample, clickable      ]
  [ master vol ]                                    [ MIDI: OMNI v ]

## Acceptance tests
1. Load 3 different SF2s (a GM bank, a single-instrument bank, a synth
   bank); each lists presets and plays without crash.
2. AS-AUTHORED: a held note on a looped preset sustains indefinitely
   without clicks; a no-loop (one-shot) zone plays through once.
3. LOOP-ONLY: the same held note starts directly in the sustain character
   (no attack transient audible).
4. Zone display: playing a chromatic scale across a zone boundary updates
   the readout's sample name/root exactly at the boundary key, and the
   piano strip's coloring matches where the boundary was heard.
5. Pitch: a preset's root-key note plays at the sample's native pitch;
   ±12 semitones tracks correctly (verify against a tuner on a known bank).
6. Malformed/truncated file: error message, no crash.
7. pluginval passes at strictness 5; runs in Cubase 15 as VST3.

## Build order
1. Skeleton plugin + TSF integration: load file, play AS-AUTHORED via
   TSF's own renderer, preset list. (Working sound in a day.)
2. Replace/augment playback with own read-pointer path implementing the
   two modes cleanly + release fade + voice pool.
3. Info readout + zone piano strip (parse phdr/pbag/inst/ibag/shdr into a
   zone table at load; it's all in TSF's parsed structures already).
4. Drag-drop, polish, acceptance tests.
