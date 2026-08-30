# CHANGELOG — SF2 Scout

Convention: `C:\code-bank\templates\CHANGELOG-convention.md`. Prefix `SC`.

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

## ITEM LEDGER (next free: SC-009)
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
