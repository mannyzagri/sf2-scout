# SF2SCOUT_WAV_EXTENSION.md — extend SF2 Scout with WAV loop editing (VM Claude)

> **SUPERSEDED 2026-09-12 by `docs/SCOUT_v2_SPEC.md`** (kept verbatim; Slot W
> is now the editor/export slot for EVERY source, not only user WAVs).

## Summary

SF2 Scout grows a second capability: alongside auditioning SF2 files, it
now loads the user's own WAV files, lets him set/audition loop points
(forward AND ping-pong), and SAVES the loop metadata back into the WAV as
a standard smpl chunk. This merges the planned "Loop Bench" core into the
Scout so one tool covers the full workflow: audition SF2 reference ->
load own hardware recording -> craft its loop -> A/B against the
reference -> save tagged WAV (which ROM_REPLACEMENT.md's build then
consumes). Supersedes LOOP_BENCH_SPEC.md as a separate app; that spec's
sections referenced below travel INTO the Scout.

## UI model: two source slots

- **Slot R (Reference)**: the existing SF2 side — unchanged behavior
  (preset list, AS-AUTHORED / LOOP-ONLY modes, zone map, info readout).
- **Slot W (Work)**: a WAV loaded by drag-drop or LOAD button. Gets a
  waveform editor panel (see below). WAV files dropped on the window go to
  Slot W; .sf2 files go to Slot R — route by extension.
- **Focus switch**: a prominent R / W / SPLIT selector deciding what MIDI
  plays:
  * R: keys play the SF2 preset (as today)
  * W: keys play the work WAV (transposed from its root key)
  * SPLIT: keys below the split point (default C4, draggable) play R,
    above play W — for direct same-session comparison. (A LAYER mode is
    NOT required; split is enough and avoids level-balance questions.)

## WAV editing features (from LOOP_BENCH_SPEC.md, folded in)

Adopt these sections of LOOP_BENCH_SPEC.md verbatim as requirements for
Slot W, with section numbers as in that doc:
- §1 File I/O: WAV 16/24/32f mono/stereo load; smpl chunk read on load
  (existing markers appear); SAVE (Ctrl+S) writes smpl (loopStart,
  loopEnd, type 0=fwd / 1=ping-pong, dwMIDIUnityNote, pitch fraction) +
  bext provenance text; optional 16-bit/44.1k-mono export checkbox;
  SAVE AS with <PREFIX>_<NOTE>.wav auto-naming.
- §2 Waveform display + markers: zoomable waveform, draggable LOOP
  START/END markers, snap-to-zero-crossing toggle, keyboard nudge
  (±1/±100 samples), root key + fine tune fields with AUTO-DETECT,
  seam view panel (loop-end butted to loop-start, live while dragging).
- §3 Playback: forward / PING-PONG / off loop modes on F/P/O keys —
  ping-pong must reverse direction sample-accurately at both markers;
  RELEASE fade knob (loop keeps cycling under fade after note-off) and
  ATTACK fade-in knob; playhead on waveform and seam view.
- §5 Sample-accurate values everywhere: integer sample fields for all
  positions/lengths, editable loop-length field, EXPORT RANGE by exact
  sample values with marker remap, live LENGTH readout, cursor position
  in samples.
- §4 assists (SUGGEST button, period readout, CLICK METER) are
  nice-to-have in this merge — implement if time allows, cut first if not.

## Interactions between the two slots
- The mode switch (AS-AUTHORED/LOOP-ONLY) applies to Slot R only; the
  loop-mode keys (F/P/O) apply to Slot W only. Two clearly separated
  control groups, labeled R and W.
- Info readout panel becomes two-column when both slots are loaded:
  R column = SF2 zone info (as today); W column = work WAV's root, loop
  points (samples/ms/periods), loop type, file length.
- No audio routing between slots, no export of anything from Slot R —
  the Scout's no-SF2-export rule (SF2_AUDITIONER_SPEC.md Non-features)
  REMAINS ABSOLUTE. Only Slot W (the user's own file) can be saved.

## Acceptance (additions to the Scout's existing tests)
1. Drop JD_STR1_C4.wav -> waveform appears in Slot W; set markers; hold a
   MIDI key in W focus -> forward loop sustains cleanly; press P ->
   ping-pong audibly bounces; Ctrl+S; reload file -> markers restored.
2. Saved file opens in Cubase with identical loop points (smpl inclusive-
   end handled consistently).
3. SPLIT mode: below C4 plays the SF2 preset, above plays the WAV, both
   transposed correctly.
4. A WAV with an existing smpl chunk (e.g. from Polyphone or a previous
   session) shows its markers and loop type on load; type edits persist.
5. EXPORT RANGE with typed sample values produces an exactly-sized file
   with remapped markers (LOOP_BENCH_SPEC acceptance 8).
6. No control path exists that writes SF2 sample data to disk.

## Build note
Reuse the Scout's voice/MIDI/transport code for Slot W playback — the WAV
player is one more sample source for the same voice pool, not a second
engine. The waveform editor is the main new UI surface; keep it plain
JUCE, function first.
