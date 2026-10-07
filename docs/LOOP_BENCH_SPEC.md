# LOOP_BENCH_SPEC.md — "Loop Bench" sample loop editor/auditioner (VM Claude task)

> **SUPERSEDED 2026-09-12 by `docs/SCOUT_v2_SPEC.md`** (kept verbatim).

## Purpose

The user's half of the bank-v4 pipeline: load a hardware-recorded WAV, find/
set loop markers BY EAR, audition forward vs ping-pong looping with real MIDI
input, set a release fade, then SAVE the file with embedded loop metadata
(smpl chunk) that the bake pipeline reads as ground truth. One person, one
window, one file at a time, fast iteration.

Standalone app FIRST (primary use), VST3 second if free. JUCE 8/C++17/CMake
per ENVIRONMENT.md. Plain JUCE UI. This is a craftsman's bench tool —
function, snappiness, keyboard shortcuts over beauty.

## Core workflow (design everything around this loop)
  drag WAV in → see waveform → drop loop markers → hold a MIDI key to hear
  it loop → nudge markers → toggle fwd/pingpong → set release fade →
  CTRL+S saves WAV+smpl → next file.

## Features

### 1. File I/O
- Drag-and-drop WAV (16/24/32f, mono/stereo, any SR) + LOAD button.
- If the file already has a smpl chunk: LOAD ITS LOOP POINTS and root key
  into the editor (round-trip editing must work).
- SAVE (Ctrl+S): writes WAV with:
  * smpl chunk: loopStart, loopEnd, loop type (0=forward, 1=ping-pong),
    dwMIDIUnityNote (root key), dwMIDIPitchFraction (fine tune)
  * bext chunk (BWF): Originator "LoopBench", OriginationDate, Description
    free-text field editable in a small text box (provenance notes, e.g.
    "JD990 own patch SILKSTR, dry, UFX2")
  * Audio: by default SAME bit depth/SR as loaded (no silent conversion).
    Optional "Export 16-bit/44.1k mono" checkbox for bank-ready output
    (stereo→mono fold with -3dB pan law).
- SAVE AS with auto-naming help: <PREFIX>_<NOTE>.wav where NOTE is derived
  from the root key (e.g. JD_STR1_C4.wav). Prefix box remembers last value.
- NO destructive editing of the source file unless SAVE overwrites it.

### 2. Waveform display + loop markers
- Full-file waveform (min/max peaks), zoom (mouse wheel around cursor,
  shift-drag pan), down to sample level at the markers.
- Two draggable markers: LOOP START (green), LOOP END (red). Number boxes
  for exact sample values. Snap-to-zero-crossing toggle (default ON).
- **Seam view**: a dedicated small panel showing the splice: the last N ms
  before loopEnd butted against the first N ms after loopStart (N zoom
  10-100ms) so discontinuities are VISIBLE. Update live while dragging.
- Marker nudge keys: [ ] for start -/+ one sample, { } x100; same for end
  with ; ' and : ". (Exact keys negotiable, but keyboard nudge is REQUIRED
  — mouse-only marker placement is too coarse for click hunting.)
- Root key + fine tune fields (dwMIDIUnityNote / cents). AUTO-DETECT button:
  autocorrelation pitch on the loop region fills both (user can override).

### 3. Playback / audition engine
- MIDI input: all devices/omni (user has a Novation Launchkey), note-on
  plays the sample transposed by (note - rootKey) semitones + fine tune,
  simple linear-interp varispeed. Velocity → level (sqrt curve). 8 voices.
- Also: SPACE = latch-play at root key (no MIDI needed), computer-keyboard
  piano (Z-M row) as fallback.
- **Loop modes** (the audition core):
  * FORWARD: play from 0, wrap [loopStart, loopEnd) while held.
  * PING-PONG: same but reverse direction at loopEnd and loopStart.
  * OFF: play through once (for judging the raw file).
  Toggle with F / P / O keys, prominent buttons + LED state.
- **Release behavior** (per user requirement): on note-off, the loop
  CONTINUES cycling while a release fade-out is applied. RELEASE knob
  10ms..5s (log). Also an ATTACK fade-in knob 0..500ms applied at note-on
  (compensates clicky region starts; default 5ms).
  These two are AUDITION-side; on SAVE they are written into the bext
  Description text (e.g. "rel=800ms att=10ms") as advisory metadata for
  the bake (smpl has no fade fields) — the bank's per-tone envelopes are
  the real release at play time.
- Playhead drawn on the waveform AND in the seam view while playing.
- A/B: key TAB toggles between current markers and last-saved markers for
  instant comparison.

### 4. Assists (cheap, high value)
- "SUGGEST" button: auto-search the region around the current markers
  (±200ms) for the locally best splice (zero-crossing + slope + short
  spectral match) and move markers there. This is an ASSIST that refines a
  by-ear placement, not an oracle. Show the score. (Port the scoring from
  the session's loop-search: junction value/slope diff + 2048-pt spectral
  diff + RMS diff.)
- Loop length readout in samples / ms / PERIODS (using detected pitch) with
  a warning tint when length is not near an integer period count.
- CLICK METER: after any marker move, render 4 loop passes silently,
  measure seam derivative vs signal 99.5th percentile, show ratio as a
  green/yellow/red lamp (<1 green, 1-2 yellow, >2 red). This is the
  measured "will it click" answer next to the by-ear one.

### 5. Sample-accurate values everywhere (REQUIRED)
The user works by exact sample counts (his DAW exports are sample-precise);
the bench must speak that language natively:
- EVERY position/length field is an editable integer SAMPLE value first:
  loopStart, loopEnd, loop length, selection start/end, total length.
  Secondary readouts in ms and periods, derived, read-only.
- Direct typing into any field commits on Enter, clamps to valid range,
  and snaps markers accordingly (typing overrides zero-crossing snap).
- Loop length field is EDITABLE: typing a length moves loopEnd to
  loopStart+length. A "×2 /2 +1 -1" nudge row next to it for quick
  period-multiple experiments.
- **TRIM/EXPORT BY SAMPLE RANGE**: an export range [start, end) in samples,
  settable by typing exact values or from the current selection, with an
  "EXPORT RANGE" action that writes exactly (end-start) samples — no
  rounding, no hidden fades (any fade must be explicitly enabled). Loop
  markers falling inside the range are remapped (marker - start) and
  written to the exported file's smpl chunk; if markers fall outside, warn
  before export.
- A LENGTH= readout always visible: exported file will be N samples / N/SR
  seconds / N*bytes-per-frame KB, updating live.
- Selection→range and range→selection buttons so visual selection and
  numeric range interconvert.
- Status bar shows cursor position under mouse in samples (and ms) at all
  times.

### 6. Explicitly OUT of scope
- No multi-file/bank editing, no SF2 export (v2 maybe), no FLAC (loop tags
  non-standard), no DSP beyond varispeed+fades (no filter/FX — audition
  dry), no crossfade-baking into the audio (the BAKE does seam crossfades;
  the bench only sets markers on untouched audio), no recording.

## smpl chunk implementation notes
- Standard RIFF 'smpl': manufacturer/product 0, samplePeriod=1e9/SR,
  dwMIDIUnityNote=rootKey, dwMIDIPitchFraction=cents*0x80000000/50 per spec
  (verify rounding), one sample loop: identifier 0, type 0|1, start, end
  (INCLUSIVE end per spec — off-by-one matters, validate against Cubase's
  reading), fraction 0, playCount 0 (infinite).
- Validate round-trip: save → reload in Loop Bench = identical markers;
  ALSO open in Cubase Sample Editor and confirm Cubase shows the same loop.
  That cross-check is an acceptance test, not optional.

## Acceptance tests
1. Load 32-bit float stereo Cubase export; edit; save 16-bit mono with
   smpl; reload — markers identical; Cubase shows same loop points.
2. Forward and ping-pong audition audibly differ and wrap correctly at
   sample-accurate positions (no drift over 60s hold).
3. Note-off during loop → loop keeps cycling under release fade; no click
   at note-off; release knob range works at both extremes.
4. MIDI from a Launchkey plays transposed correctly across ±2 octaves from
   root (verify against tuner).
5. Seam view updates live during marker drag at 60fps-ish on a 15s file.
6. CLICK METER: deliberately bad marker (mid-waveform, non-zero) shows red;
   SUGGEST then finds a green/yellow placement nearby.
7. A file with an existing smpl chunk from another tool (e.g. exported by
   Polyphone) loads its markers correctly.
8. EXPORT RANGE with typed values start=54354 end=98201 produces a file of
   exactly 43847 samples, with remapped smpl markers; re-import confirms.

## Build order
1. Load/display/zoom + markers + forward-loop audition at root key. (Core
   value on day one.)
2. MIDI in + transpose + voices + release/attack fades + ping-pong.
3. smpl/bext writing + round-trip + Cubase validation.
4. Seam view, SUGGEST, CLICK METER, A/B, polish.
