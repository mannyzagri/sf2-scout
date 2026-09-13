# CHANGELOG — SF2 Scout

Convention: `C:\code-bank\templates\CHANGELOG-convention.md`. Prefix `SC`.

## v0.6.0 — 2026-09-13 — GUI handoff v2 landed: the whole v2 face + build orders 2–4 behind it (deployed 2026-09-13, pluginval 5)
Host impact: reload instance (no param-list change; the non-param tree layout changed — `sources` / `edits` children, slot / export properties; a 0.5.0 session restores nothing but its params). Window is now 1200 × 840, resizable with a fixed aspect.

### Engine
- [SC-031] CHANGED: `ScoutEngine` plays decoded samples in THREE slots (A, B, Cur) — every SF2 zone, module sample and WAV enters it as a `WavSample`; the SF2 preset-by-key-range player is gone (D-9). Keyboard routing `KbMode` A / B / SPLIT (split note) / TOGGLE with fallback to the loaded slot (`routeSlot`); per-slot loop / tuning / fades / playhead / last note; LOOP-ONLY starts inside the loop for any sample; 16 voices (D-10). Slot handoff keeps the one-retiree rule and now hands a never-played pending sample back to the caller (`setSlot` / `clearSlot` return it; `forgetAll` for shared owners).
- [SC-032] ADDED: `Assists` (JUCE-free): LOUDNESS (RMS dB of the loop), PERIODS (loop length in root periods, near-integer test), CLICK METER (4-pass seam render, derivative vs the 99.5th percentile), SUGGEST (best END within ±200 ms: value + slope continuity, 2048-point spectral cosine distance, RMS match), AUTO-DETECT ROOT (normalised autocorrelation, smallest lag within 10 % of the peak, parabolic refinement → note + cents).
- [SC-033] ADDED: `exportWav` — re-encodes from the decoded floats: EXPORT RANGE `[start, end)` with marker remap (a loop outside the range is dropped, smpl keeps the root), 16-bit / 44.1 kHz mono conversion (windowed-sinc resampling + TPDF dither, markers scaled), stereo fold SUM (−3 dB) / L ONLY (D-7), native rate/depth otherwise (8-bit → 16, floats stay float); bext Originator "Scout v2" + date/time. `writeWav` (SAVE) unchanged apart from the Originator.
- [SC-034] ADDED: `SoundFontBank::info()` — the file's LIST/INFO sub-chunks (INAM, IENG, ICOP, ICMT, ISFT, isng, ifil …) for the METADATA box.
- [SC-035] CHANGED: harness re-targeted to the slot model and extended: `[last-note]` `[routing]` `[routing-fallback]` (A/B/SPLIT/TOGGLE + empty-slot fallback), `[export]` (range length + remap/drop, 16-bit/44.1 mono, L-only, bext fields), `[assists]`, INFO list in `[load]`, decoded-zone playback in `[authored]` `[looponly]` `[pitch]` `[release]` `[stereo-pair]` `[pool-bound]`, slot semantics in `[swap]` `[unload]`. **347 checks**, 27 sections (was 284 / 25).
### Processor
- [SC-036] CHANGED: sources model — several sources open at once (`openSource` routes .sf2 / module / .wav; a failed load adds a red error row instead of a dialog), `unloadSource`, selection (source, preset, current sample by `SampleKey`), CONTENTS rows per source, per-sample SESSION EDITS (`SampleEdits`: markers, loop kind + override, root / cents, attack / release, prefix, description) initialised from the sample's own metadata, decoded-sample cache with engine keep-alive (`engineHeld_`), A/B slots + keyboard mode + split + TAB toggle, latch (PLAY / SPACE) on the current sample, QWERTY audition routed like MIDI.
- [SC-037] ADDED: export settings (folder + "reference" / "rom" quick targets remembered in `%APPDATA%\SF2 Scout\SF2 Scout.exports`, NATIVE / 16b-44.1 MONO, SUM / L ONLY, range, scope, skip-existing), EXPORT SAMPLE (`<SOURCE>_<NN>_<name>_<NOTE>.wav`), EXPORT RANGE (`…_r<start>-<end>.wav`), BATCH (preset / module / file or whole source; one file per timer tick, cancel, progress, last-4 log, skip-existing), SAVE (WAV sources only: smpl + bext back into the file) and SAVE AS (`<PREFIX>_<NOTE>.wav`; byte-identical for WAVs, native re-encode for decoded samples), METADATA rows (SF2 INFO / MODULE INFO incl. message + instrument names / WAV chunks).
- [SC-038] CHANGED: session state — sources by path (order-indexed keys), `edits` per sample, `selSrc` / `cur` / `slotA` / `slotB`, `kbMode` / `splitNote` / `toggleOn`, export settings, `uiScale`; restore reloads the sources (a missing file becomes an error row), re-applies edits and slots.
### GUI
- [SC-039] CHANGED: the v2 face, verbatim from `docs/handoff-gui-v2/` at the manifest's bounds — header (LOAD, SOURCES OPEN, KEYBOARD PLAYS A/B/SPLIT/TOGGLE + split note, MODE), SOURCE LIST tree with type badges, CONTENTS table (#, NAME + A/B tags, FMT, FRAMES, LP, HZ; → A, → B, ‹ ›), readout A | B (slot square, badge, path, live dot, PLAYED / ROOT / STRETCH, rows), zone map following the slot that holds an SF2 zone (split note marker), EDITOR (title + status, PLAY/STOP, LOOP, ZERO-X SNAP, SUGGEST, AUTO-DETECT ROOT, SAVE AS, SAVE; 140-px waveform with snap-on-release; seam; fields incl. PERIODS warn tint, CLICK METER, LOUDNESS, PREFIX, BEXT DESCRIPTION; status line), EXPORT column (folder + quick targets, AUDIO / STEREO, EXPORT SAMPLE + target name, RANGE, BATCH + progress + log, METADATA box), footer (MASTER, dB, stamp, QWERTY hint, DEVICE chip, MIDI IN chip, voices). Window = 1200 × 840 canvas scaled uniformly 0.75–1.5 (fixed aspect; OPEN QUESTION 11).
- [SC-040] ADDED: IBM Plex Sans / Mono bundled (`third_party/fonts/`, OFL 1.1) as BinaryData; `ui/Fonts.h` builds fonts by CSS px (point height) and letter-spacing in px. Retires D-2.
- [SC-041] ADDED: hotkeys F / P / O, `[ ] { }` `; ' : "` nudges, `, .` step, TAB swap A/B, SPACE latch, L audition mode, Ctrl+S / Ctrl+Shift+S; QWERTY piano Z–M (C4–B4) with key-up tracking.
- [SC-042] FIX (found in the eyeball pass): a project settings file named `SF2 Scout.settings` would have shared the standalone wrapper's file — renamed `.exports`; footer DEVICE chip moved to the README's flex position (D-8); the window scale follows the smaller dimension so a host that ignores the aspect constraint cannot distort the face.
### Docs / comms
- [SC-043] `docs/handoff-gui-v2/` landed (README, native manifest, prototype, support.js); `validator.json` handoff pointer + harness expectations updated; `docs/handoff-gui-v1/` is history. Lint reply + manifest schema sent to gui-claude (mailbox `VM-HANDOFF-V2-LINT-20260913.md`); brief bundle + current-face HTML mockup on the share (`Projects\sf2-scout\`). Version 0.6.0.

## v0.5.0 — 2026-09-12 — v2 build order 1: tracker modules, SF2 zone → W, export from any source (deployed 2026-09-12, pluginval 5)
Host impact: reload instance (no param-list change; two new non-param tree properties `modulePath` / `moduleSample`)

### Scope
- [SC-024] CHANGED: scope canon is now `docs/SCOUT_v2_SPEC.md` (operator directive, received verbatim from the share) — universal vintage sample player / editor / converter. Export is a first-class feature; the "no export / no save" NON-features are retired. SF2 and module files remain read-only containers. `CLAUDE.md` scope layer, `SSOT.md` amendment (UNSIGNED), superseded banners on `docs/DSP.md`, `docs/SF2SCOUT_WAV_EXTENSION.md`, `docs/LOOP_BENCH_SPEC.md`.
### Engine
- [SC-025] ADDED: vendored **libopenmpt 0.8.9** soundlib (BSD-3, `third_party/libopenmpt/`, 6 MB of sources, autotools/tests/docs stripped) as static lib `openmpt_soundlib`; no zlib/mpg123/vorbis (MO3 and vorbis-compressed samples report an error, every plain tracker format loads). The public libopenmpt API exposes no sample PCM, so `OpenMPT::CSoundFile` is read directly; `MPT_ASSERT` is routed to a no-op `AssertHandler`.
- [SC-026] ADDED: JUCE-free `ModuleSource` — parses any libopenmpt format from memory (patterns/plugins skipped), exposes title / format / made-with / song message / instrument names and a per-sample table (name, bits, channels, frames, C-5 rate, loop + ping-pong, IT sustain loop, default volume); `decode(index)` → `WavSample` (8→16-bit, inclusive loop end, sustain loop preferred while held, root 60 / 0 c with the C-5 frequency as the WAV rate, bext provenance). Extension routing via `CSoundFile::IsExtensionSupported`.
- [SC-027] ADDED: `WavSample::fromPcm16` (the bridge every non-WAV source uses to enter Slot W: synthesises fmt + data chunks so `writeWav` serialises it unchanged) and `SoundFontBank::decodeZone` (zone sample out of the float pool → 16-bit WavSample with the zone's loop, root − coarseTune, fine cents, provenance; a stereo pair exports as two mono halves).
- [SC-028] ADDED: harness `[mod-load]` (in-memory ProTracker MOD: sample table, loop flags, PAL C-5 rate 8287 Hz, decode → export → reload round-trip, extension routing, junk/truncated/lying-header input) and `[mod-play]` (decoded sample sounds in Slot W at its C-5 rate and loops). 284 checks. The harness is now the CMake target `test_engine` (links `openmpt_soundlib`); `--probe <module> [outDir]` lists a real file's samples and exports them. Probe-verified on real MOD / XM (ping-pong) / IT 2.14 (compressed samples, ping-pong).
### Processor / GUI
- [SC-029] ADDED: `loadModule` / `selectModuleSample` / `sendZoneToWav` on the processor (shared `installWav` tail with `loadWav`); a module or an SF2 zone decoded into Slot W has no file path, so SAVE routes to SAVE AS (`wavIsDecoded`), SAVE AS suggests `<container>_<sampleName>_<NOTE>.wav` next to the container. Session state persists `modulePath` + `moduleSample` and restores the module before a plain WAV path.
- [SC-030] ADDED: LOAD and LOAD WAV browsers and drag-and-drop accept every libopenmpt extension (routed to Slot W); the Slot W title shows `module ▸ NN name ▾` and CLICKING it opens the sample chooser (index, name, bits, frames, loop kind, C-5 Hz); `,` / `.` step through the module's samples; a **→ W** button in the preset-list header sends the readout's zone (last played, else under the last note, else first) to Slot W, asking first if Slot W has unsaved markers. Version 0.5.0.

## v0.4.0 — 2026-09-04 — KEYBOARD PLAYS control, empty-slot fallback, UNLOAD per slot (unreleased, not deployed)
Host impact: reload instance (no param-list change)

### Engine
- [SC-020] FIX: with the focus left on WAV or SPLIT and no WAV loaded (or after a failed load), the keyboard went silent — routing sent the notes to an empty Slot W. `routesToWav` now falls back to the loaded slot at note time (WAV/SPLIT → SF2 when no WAV; SF2/SPLIT → WAV when no SF2), RT-safe; harness `[focus-fallback]`. Focus verification otherwise found the hardware path sound: standalone holder → processBlock → `noteOn` → focus atomics pushed by every state change; readout/playhead follow the sounding slot (R readout + loop bar for SF2, W column + waveform playhead for the WAV).
- [SC-021] ADDED: `ScoutEngine::clearBank` / `clearWav` — UNLOAD path: the audio thread kills only that slot's voices and parks the data for the collector (same one-retiree rule as a swap; a clear waits if the collector is behind, a never-consumed pending bank is dropped). Bank swaps no longer kill WAV voices. Harness `[unload]`.
### GUI
- [SC-022] CHANGED: the header focus switch is now a labelled **KEYBOARD PLAYS: SF2 / WAV / SPLIT** segmented control (larger, accent highlight), split-note field beside SPLIT; WAV and SPLIT grey out (and refuse clicks) while no WAV is loaded, SF2 and SPLIT while no SF2 is loaded; the highlighted choice is normalised to a loaded slot on every load/unload so the control never shows a slot that is not the one sounding.
- [SC-023] ADDED: UNLOAD buttons — Slot R (preset-list header) and Slot W (control row). Each stops the slot's voices via SC-021, clears list/readout or waveform/seam/fields, clears the stored path in the tree, unlatches PLAY C3 (W), and moves the KEYBOARD PLAYS choice to the other loaded slot. Unloading a WAV with unsaved marker edits asks Save / Discard / Cancel first. Harness 249 checks. Version 0.4.0.

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

## ITEM LEDGER (next free: SC-031)
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
| SC-020 | fixed | 0.2.0 | 0.4.0 | focus on an empty slot muted the keyboard |
| SC-021 | shipped | 0.4.0 | — | engine clearBank/clearWav (UNLOAD path) |
| SC-022 | shipped | 0.4.0 | — | KEYBOARD PLAYS SF2/WAV/SPLIT control with disabled empty slots |
| SC-023 | shipped | 0.4.0 | — | UNLOAD buttons per slot, save prompt on dirty WAV |
| SC-024 | shipped | 0.5.0 | — | v2 scope canon (SCOUT_v2_SPEC.md), export allowed, containers read-only |
| SC-025 | shipped | 0.5.0 | — | vendored libopenmpt 0.8.9 soundlib as openmpt_soundlib |
| SC-026 | shipped | 0.5.0 | — | ModuleSource: module parse + sample decode → WavSample |
| SC-027 | shipped | 0.5.0 | — | WavSample::fromPcm16 bridge + SoundFontBank::decodeZone |
| SC-028 | shipped | 0.5.0 | — | harness mod-load / mod-play, CMake test_engine target, --probe |
| SC-029 | shipped | 0.5.0 | — | processor loadModule / selectModuleSample / sendZoneToWav, state |
| SC-030 | shipped | 0.5.0 | — | module routing in browsers/drag, sample chooser, , . keys, → W button |
