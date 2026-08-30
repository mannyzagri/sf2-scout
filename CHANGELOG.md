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
### Infra
- [SC-005] ADDED: scaffold — CLAUDE.md, SSOT.md (unsigned), PROJECT-NOTES STATE, validator.json (dsp/bundle/deploy/host), FEATURE-INDEX.json, comms/, share mailbox.

## ITEM LEDGER (next free: SC-006)
| id | status | introduced | resolved | summary |
|----|--------|-----------|----------|---------|
| SC-001 | shipped | 0.1.0 | — | SF2 loading + zone table |
| SC-002 | shipped | 0.1.0 | — | two-mode voice player |
| SC-003 | shipped | 0.1.0 | — | engine harness |
| SC-004 | shipped | 0.1.0 | — | plain-JUCE face (handoff v1) |
| SC-005 | shipped | 0.1.0 | — | project scaffold |
