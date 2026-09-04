# CHANGELOG — SF2 Scout

Convention: `C:\code-bank\templates\CHANGELOG-convention.md`. Prefix `SC`.

## v0.3.0 — 2026-09-04 — operator feedback on 0.2.0: MIDI device, marker placement, PLAY (unreleased, not deployed)
Host impact: reload instance (no param-list change)

### GUI
- [SC-017] FIX: loop markers could not be positioned by mouse — the only placement path was grabbing an existing 2 px line within ±6 px, and the default whole-file loop (no `smpl`) put those lines at x=0 / x=width (half clipped by the border) or off-screen once zoomed in. Now: left click anywhere places and drags the NEARER marker, click near a line (±8 px) grabs it, right-drag always moves END, shift/middle-drag pans; lines are clamped visible at the file edges; drag cursor on hover; markers echo into the loopStart/loopEnd/length fields live and typed values move the cursors. Hit-test/drag math is JUCE-free (`Source/engine/LoopMarkers.h`) and pinned by harness `[markers]`.
- [SC-018] ADDED: latching PLAY C3 button in the Slot W band — note-on/off for MIDI 48 on the WAV regardless of focus, through the same lock-free FIFO as MIDI/zone-strip audition (`ScoutEngine::noteOnWav`); loop mode F/P/O and live marker edits apply while it plays; unlatch = release fade; a new WAV load unlatches it.
### Infra
- [SC-019] ADDED (standalone only): MIDI input DEVICE selector in the footer — enumerates `juce::MidiInput::getAvailableDevices()`, re-polls once a second (plug/unplug rebuilds the list, a vanished device falls back to ALL), remembered as non-param tree property `midiInputDevice`; enables the chosen (or all) inputs on the standalone's AudioDeviceManager. Root cause of the silent keyboard: JUCE's standalone enables NO MIDI inputs by default — now ALL are enabled unless one is chosen. Hidden in the VST3 (host feeds MIDI). Harness 217 checks. Version 0.3.0.

## v0.2.0 — 2026-09-04 — Slot W: WAV loop editing (unreleased, not deployed)
Host impact: reload instance (no param-list change; editor now requests keyboard focus)

### Engine
- [SC-009] ADDED: Slot W — a WAV (16/24/32-bit PCM, 32/64f, mono/stereo) is one more sample source for the SAME 32-voice pool: lock-free `setWav`/`takeRetiredWav` handoff, transpose from root key + fine tune, loop modes FORWARD / PING-PONG (sample-accurate reflection at both markers) / OFF, live loop-point edits via one packed atomic, note-off keeps the loop cycling under an adjustable RELEASE fade, ATTACK fade-in.
- [SC-010] ADDED: focus switch R / W / SPLIT (split point default C4=60): R keys play the SF2 preset, W keys the WAV, SPLIT routes below → R, at/above → W.
- [SC-011] ADDED: JUCE-free `WavSample` reader/writer — reads `smpl` (loopStart, inclusive loopEnd, type, dwMIDIUnityNote, dwMIDIPitchFraction) and `bext`; root fallback from `<PREFIX>_<NOTE>.wav`, else 60; SAVE rewrites every original chunk byte-identical and re-emits one `smpl` (inclusive end, Cubase-consistent) + one `bext` with a provenance description; optional 16-bit mono export (-3 dB fold). This is the project's only writer and it can only serialise a WavSample — Slot R stays read-only.
- [SC-012] ADDED: harness sections `[wav-load]` `[wav-pitch]` `[wav-forward]` `[wav-pingpong]` `[wav-oneshot]` `[wav-release]` `[wav-focus]` `[wav-smpl]` — 197 checks total (was 112).
### GUI
- [SC-013] ADDED: Slot W band below the zone map — LOAD WAV + drag-drop (.wav → W, .sf2 → R), zoomable waveform (wheel zoom about cursor, shift-drag pan) with draggable START/END markers, zero-crossing snap toggle, seam view (loop end butted to loop start, live, wheel-zoomable 1–200 ms, step readout), playhead on both, integer sample fields for loopStart / loopEnd / loop length (commit on Enter, clamped), root + fine-tune + attack + release fields, SAVE / SAVE AS (`<PREFIX>_<NOTE>.wav`), 16-BIT MONO checkbox, bext description + prefix boxes, cursor/loop status line. Keys: F/P/O loop mode, `[` `]` `{` `}` start ±1/±100, `;` `'` `:` `"` end ±1/±100, Ctrl+S save, Ctrl+Shift+S save as.
- [SC-014] ADDED: header FOCUS switch R / W / SPLIT + split-note field; info readout becomes two-column when a WAV is loaded (W: played, root, stretch, loopStart/End in samples + ms, loop len in samples/ms/periods with off-integer warning tint, type, length, format).
### Infra
- [SC-015] CHANGED: version 0.2.0; `EDITOR_WANTS_KEYBOARD_FOCUS TRUE` (keyboard shortcuts need it); Slot W state persisted as non-param APVTS tree properties (`wavPath`, `wavLoop*`, `wavRoot`, `wavCents`, `wavAttackMs`, `wavReleaseMs`, `focus`, `splitNote`, `wavExport16Mono`, `wavDescription`, `wavPrefix`).
### Removed / Deferred
- [SC-016] DEFERRED: EXPORT RANGE by sample values with marker remap, SUGGEST, CLICK METER, AUTO-DETECT root, TAB A/B markers, computer-keyboard piano (LOOP_BENCH_SPEC §4/§5 nice-to-haves) — next pass.

## v0.1.0 — 2026-08-30 — kickoff build (unreleased, not deployed)
Host impact: n/a (never loaded in a host yet)

### Engine
- [SC-001] ADDED: SF2 loading via vendored TinySoundFont v0.9 + own `shdr` walk; preset list; zone table with sample names, key/vel ranges, root, loop points, rate, stereo flag.
- [SC-002] ADDED: own 32-voice read-pointer player — AS-AUTHORED and LOOP-ONLY modes, 80 ms release fade, oldest-note stealing, sqrt velocity, ±2 st pitch bend, omni/channel filter.
- [SC-003] ADDED: JUCE-free cl.exe harness `tests/test_engine.cpp` fabricating a complete SF2 in memory; 58 checks.
### GUI
- [SC-004] ADDED: plain-JUCE fixed face recreating gui-claude handoff v1 — header, preset list + steppers, info readout, loop bar with live playhead, clickable zone map + labels, footer master/MIDI/voices. Drag-and-drop + LOAD browser. Build stamp painted in the footer.
### Fixed (architect review, pre-release)
- [SC-006] FIX: a corrupt scaleTuning/coarseTune could spin the audio thread forever in the loop wrap — generators clamped at load, step clamped, fmod wrap.
- [SC-007] FIX: host `setStateInformation` off the message thread raced the editor and the bank collector — restore now dispatched to the message thread; widgets copy Zone data by value; retiree collected before every swap.
- [SC-008] FIX: readout could show a torn note/zone tuple — proper seqlock; region→sample joined by sampleID (TSF patch D-6) instead of offset; pool bound from TSF's clamp; basic L/R pairs hard-panned; channel filter no longer strands held notes; UI note-off never dropped.
### Infra
- [SC-005] ADDED: scaffold — CLAUDE.md, SSOT.md (unsigned), PROJECT-NOTES STATE, validator.json (dsp/bundle/deploy/host), FEATURE-INDEX.json, comms/, share mailbox.

## ITEM LEDGER (next free: SC-020)
| id | status | introduced | resolved | summary |
|----|--------|-----------|----------|---------|
| SC-001 | shipped | 0.1.0 | — | SF2 loading + zone table |
| SC-002 | shipped | 0.1.0 | — | two-mode voice player |
| SC-003 | shipped | 0.1.0 | — | engine harness |
| SC-004 | shipped | 0.1.0 | — | plain-JUCE face (handoff v1) |
| SC-005 | shipped | 0.1.0 | — | project scaffold |
| SC-006 | fixed | 0.1.0 | 0.1.0 | hostile pitch generators spin audio thread |
| SC-007 | fixed | 0.1.0 | 0.1.0 | setState off-thread load / widget UAF |
| SC-008 | fixed | 0.1.0 | 0.1.0 | readout truth: seqlock, sampleID join, pool bound, L/R pan, channel filter, UI note-off |
| SC-009 | shipped | 0.2.0 | — | Slot W WAV voice path in the shared pool (fwd / ping-pong / off, fades) |
| SC-010 | shipped | 0.2.0 | — | focus switch R / W / SPLIT |
| SC-011 | shipped | 0.2.0 | — | WavSample smpl/bext reader + byte-identical writer |
| SC-012 | shipped | 0.2.0 | — | harness wav-* sections (197 checks) |
| SC-013 | shipped | 0.2.0 | — | Slot W editor band (waveform, seam, fields, keys, save) |
| SC-014 | shipped | 0.2.0 | — | header FOCUS switch, two-column readout |
| SC-015 | shipped | 0.2.0 | — | v0.2.0, keyboard focus flag, Slot W state persistence |
| SC-016 | deferred | 0.2.0 | — | EXPORT RANGE, SUGGEST, CLICK METER, AUTO-DETECT, A/B, kbd piano |
| SC-017 | fixed | 0.2.0 | 0.3.0 | loop markers not positionable by mouse (grab-only, edge/off-screen lines) |
| SC-018 | shipped | 0.3.0 | — | PLAY C3 latch button (Slot W) |
| SC-019 | shipped | 0.3.0 | — | standalone MIDI input device selector; all inputs enabled by default |
