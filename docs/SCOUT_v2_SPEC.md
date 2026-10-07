# SCOUT_v2_SPEC.md — universal vintage sample player / editor / converter
(VM Claude task; consolidates and supersedes SF2_AUDITIONER_SPEC.md,
LOOP_BENCH_SPEC.md, SF2SCOUT_WAV_EXTENSION.md and its addendum)

## What it is

A standalone EXE and a VST3 that opens vintage sample sources, deploys
their embedded samples with original loop points / root keys / loop types,
plays them from MIDI, edits and tags WAVs, and EXPORTS any sample from any
source as a loop-tagged WAV. Purpose: the user compares reference samples
against his own hardware recordings in Cubase — sonically AND visually
(waveform and spectrum analyzers) — and crafts loop points for his ROM.
Export is a first-class feature of this tool.

Targets: Standalone app (primary) + VST3. JUCE 8 / C++17 / CMake per
ENVIRONMENT.md. Plain JUCE UI; snappy, keyboard-driven.

## Sources (what it opens)

| Type | Formats | Library |
|---|---|---|
| WAV | 16/24/32f, mono/stereo, any SR; reads smpl/cue/bext | JUCE |
| SoundFont | .sf2 (and .sf3 if trivial) | TinySoundFont (MIT) for parsing; own playback |
| Tracker modules | .mod .xm .it .s3m + all libopenmpt formats | libopenmpt (BSD) |

Note on Schism Tracker source (GPL-2.0): reference reading only for
format quirks; do not copy code into this project.

Drag-and-drop any of the above onto the window (route by extension) or
LOAD via native browser. Multiple sources may be open; a SOURCE LIST
panel shows them, and each source expands to its contents:
- SF2 -> presets -> zones/samples
- module -> sample list (index, name, bits, length, loop info)
- WAV -> the single sample

## Playback / audition
- MIDI in, omni (Novation Launchkey etc.), note-on plays the selected
  sample transposed from its root key + finetune; velocity -> level; 16
  voices; oldest-steal. SPACE latch-play at root; QWERTY piano fallback.
- Loop modes, global hotkeys: F forward / P ping-pong / O off (play once).
  Default per sample = the source's own loop type (SF2 sampleModes, module
  loop flags incl. IT ping-pong & sustain loops, WAV smpl type). The user
  can override; the override is what gets exported.
- Two audition variants: AS-AUTHORED (attack then loop) and LOOP-ONLY
  (start inside the loop). Hotkey toggle.
- Release: on note-off the loop keeps cycling under a RELEASE fade
  (10ms..5s, log); ATTACK fade-in 0..500ms. Both advisory-only metadata
  on export (written to bext description).
- A/B: two selection slots (A, B) — any two samples from any sources.
  SPLIT keyboard (below/above a draggable split note) or TOGGLE (TAB
  swaps which plays). For reference-vs-recording comparison on one key.
- Playhead on waveform + seam view.

## Editor (applies to any selected sample, from any source)
- Zoomable waveform (min/max), draggable LOOP START / LOOP END markers,
  snap-to-zero-crossing toggle, keyboard nudge ±1 / ±100 samples.
- Sample-accurate integer fields for every position/length; editable loop
  length; typed values commit on Enter and override snap.
- Root key + fine tune fields with AUTO-DETECT (autocorrelation on loop
  region).
- SEAM VIEW: loop-end butted to loop-start, 10-100ms zoom, live while
  dragging.
- Assists: SUGGEST (local best-splice search ±200ms: zero-crossing +
  slope + 2048-pt spectral + RMS scoring), loop length in PERIODS with
  non-integer warning tint, CLICK METER (render 4 passes, seam derivative
  vs signal 99.5th percentile; green <1 / yellow 1-2 / red >2), LOUDNESS
  readout (RMS dB of loop region) for per-wave level judgments.
- Edits to a source sample's markers live in the session (do not modify
  SF2/module files; those are read-only containers). Edits to a WAV are
  written back on SAVE.

## Export / convert (first-class)
- EXPORT SAMPLE: writes the selected sample (from WAV, SF2, or module) as
  a WAV with:
  * smpl chunk: loopStart, loopEnd (spec inclusive end — handled
    consistently with Cubase's reading), type 0=fwd / 1=ping-pong (the
    current, possibly overridden, loop mode), dwMIDIUnityNote (root),
    dwMIDIPitchFraction (fine tune). Original loop points from the source
    are carried through unless the user edited them.
  * bext chunk: Originator "Scout v2", date, Description with source
    file name, source type, original sample name/index, loop info,
    att/rel advisory values. (So provenance travels with every export.)
  * Audio: native bit depth/SR of the source by default (SF2/module
    samples come out at their stored rate and 8/16-bit->16-bit); options:
    16-bit/44.1k mono conversion (with resampling + TPDF dither), stereo
    fold rules (sum, or L-only) selectable.
- EXPORT RANGE: exact [start,end) in samples with marker remap (writes
  exactly end-start samples; warn if markers fall outside).
- BATCH EXPORT: export all samples of an SF2 preset / a whole module /
  a whole source with auto-naming <SOURCE>_<sampleName or index>_<NOTE>.wav
  into a chosen folder. Progress bar; skip-existing option.
- Export folder is the user's choice; remember last used. Optional
  "reference" vs "rom" quick-target buttons that just preset the folder.
- SAVE (Ctrl+S) on a loaded WAV = write smpl/bext back into that file
  (round-trip). SAVE AS with <PREFIX>_<NOTE>.wav auto-naming.

## Info / readout
Per selected sample: source & type, name/index, bits, sample rate,
length (samples/ms), loop start/end/length (samples/ms/periods), loop
type, root key + finetune, loudness (RMS dB). Two-column when A/B slots
are set. For SF2: zone key/vel ranges and a clickable 128-key zone-map
strip colored by sample. For SF2 and modules: the container's own
metadata (SF2 INFO: INAM/IENG/ICOP/ICMT/ISFT; module: title, message,
instrument/sample names) in a metadata box.

## Acceptance
1. Open a GM SF2, an .it with ping-pong samples, an .xm, a .mod, a 32f
   stereo WAV: all list samples, play from MIDI at correct pitch, honor
   own loops. No crashes on malformed files (error message).
2. Ping-pong audibly bounces, sample-accurate at both markers; forward
   wraps; 120s hold shows no drift (octave-folded ±2c) and no click on a
   good seam.
3. Export a module sample -> WAV opens in Cubase with the module's loop
   points shown; export an SF2 zone -> same; smpl inclusive-end verified.
4. Export a WAV after editing markers -> reload = identical markers;
   Cubase agrees.
5. EXPORT RANGE start=54354 end=98201 -> exactly 43847 samples, markers
   remapped.
6. BATCH export of a 40-sample module produces 40 correctly named files.
7. A/B SPLIT: below split plays A, above plays B, both correctly
   transposed.
8. VST3 passes pluginval strictness 5; loads in Cubase 15; standalone
   runs with MIDI from the Launchkey.

## Build order
1. Sources + playback (WAV/SF2/module load, sample list, MIDI play with
   own loops) — sound on day one.
2. Editor (waveform, markers, nudge, seam view) + loop modes + fades.
3. Export (smpl/bext writer, range export, batch, save/round-trip) +
   Cubase validation.
4. A/B slots, assists (SUGGEST/CLICK METER/LOUDNESS), metadata boxes,
   zone map, VST3 target, polish.
