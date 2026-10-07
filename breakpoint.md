# breakpoint — session-end snapshot (subordinate to PROJECT-NOTES.md STATE)

Written 2026-09-13 by vm-claude at the end of the GUI handoff v2 pass.

## Where things stand

- **0.6.0 is built, validated and deployed** (VST3 to `C:\sf2-scout\SF2 Scout.vst3`
  and `\\VBOXSVR\vagrant\Builds\SF2 Scout.vst3`, `SF2 Scout.exe` copied to the
  share `Builds\`). pluginval strictness 5 SUCCESS, harness 347 / 0. Branch
  `feat/v2`; nothing merged to `main` (still 0.1.0). Commit hash: `git log -1`.
- The whole v2 face from `docs/handoff-gui-v2/` is in, plus the engine /
  processor work it needed (three-slot decoded-sample engine, sources model,
  assists, export range / batch / conversion, metadata, session state).
  CHANGELOG SC-031..SC-043.
- The operator has NOT seen it. First look + ear pass (EAR-GATE 13–20 and the
  older 1–12) is the gate; expect a defect list.

## Assumptions the pass was built on (reversible, OPEN QUESTIONS 11–14)

scalable window (handoff) vs "fixed" (kickoff); 16 voices (spec) vs 32 (v1);
stereo sources export mono (SUM / L ONLY); manifest param-id renames ignored
(contract ids kept). Also D-7..D-10 in CLAUDE.md §6.

## Fresh-session entry ramp

1. Read CLAUDE.md, then STATE, then this file, then `comms/to-vm.md` and the
   mailbox `from-gui/` (a manifest re-emit or a defect list may be waiting).
2. `cmake --build build --config Release --target test_engine && build\Release\test_engine.exe`
   → 347 checks. `validate.ps1 -Project C:\sf2-scout` → 8 PASS.
3. To SEE the face on this VM: launch the standalone; loaded states are
   reached by injecting a session into `%APPDATA%\SF2 Scout\SF2 Scout.settings`
   and driving buttons through UI Automation (memory note `vm-gui-automation`;
   scripts were in the 2026-09-13 session scratchpad `shot/inject.ps1`).
   Mouse / keyboard injection does not work in this RDP session.
4. Test material (untracked): `scratch/modules/*.it .mod .xm`, exported WAVs in
   `scratch/modules/out/` and `scratch/export-test/`. No SF2 on the VM — the
   operator's SoundFonts are on the host.

## Known soft spots to keep an eye on

- `ScoutProcessor::installSlot`: a shared sample is kept alive in
  `engineHeld_` until every slot retired it; `~ScoutProcessor` calls
  `engine_.forgetAll()` before the maps die. Do not reintroduce `delete` on
  a pointer that came from `decoded_`.
- The readout / editor rebuild is generation-driven (`sourceGeneration()`);
  assists (click ratio, loudness) recompute at most every 120 ms.
- `handoff-lint` still reports 2 mismatches until gui-claude re-emits the
  manifest in the lint's shape (`controls` = the 3 params, `widgets` = the
  rest, contract ids). Record the operator's rulings as `rulings` entries then.
