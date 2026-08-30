# breakpoint — sf2-scout (overwrite-only; subordinate to PROJECT-NOTES.md STATE)

**Session end: 2026-08-30, vm-claude — kickoff.**

## Where things stand

- Phase 0–4 code complete: scaffold, SSOT SIGNED, architect review applied
  (3 blockers + 4 should-fix + nits), harness 112/112, plain-JUCE face wired.
- **v0.1.0 DEPLOYED** to `\VBOXSVRagrant\Builds\SF2 Scout.vst3` (+ `SF2 Scout.exe`)
  via release.ps1; pluginval 5 PASS. **Nothing heard yet** — ear pass is the gate.
- OPEN QUESTIONS 5–7 (window height, Plex fonts, keep Standalone) still open.

## Fresh-session entry ramp

1. `git pull` (mac lane not open; still the rule).
2. Read CLAUDE.md §0–§2, PROJECT-NOTES STATE, this file, `comms/to-vm.md`.
3. Fold the ear verdict: a click at the loop seam → D-5 crossfade via
   /dsp-pass (harness `[authored]` pins the seam); readout wrong at a zone
   edge → check `[sample-id]` first.
4. Any change: harness → `release.ps1 -DryRun` → `release.ps1` → `ship.ps1`.

## Git

- `main` @ e434399 "Initial commit: SF2 Scout kickoff (phase 0)" — pushed to `mannyzagri/sf2-scout` 2026-08-30.

## Artefacts of this session

- `build\Sf2Scout_artefacts\Release\VST3\SF2 Scout.vst3`
- `build\Sf2Scout_artefacts\Release\Standalone\SF2 Scout.exe`
- `scratch\test_engine.exe` (disposable)
- Mailbox opened: `C:\vagrant\mailbox\sf2-scout\README.md` + first note to
  gui-claude (`to-gui\VM-KICKOFF-MANIFEST-ASK-20260830.md`).
