# SF2 Scout — project notes

Last updated: 2026-08-30 (kickoff, vm-claude)

## STATE (overwrite-only; CURRENT state — never append history here)

| Item | Value |
|---|---|
| Deployed | **nothing deployed yet.** First build `0.1.0` produced locally 2026-08-30 (see breakpoint for the artefact paths); not copied to `C:\vagrant\Builds\` and not auditioned. Binary literal version string `0.1.0`. |
| Validator | 2026-08-30: `dsp` stage = 1 harness `test_engine` (58 checks: load/zone join, AS-AUTHORED, LOOP-ONLY, pitch, release/steal, malformed, bank swap) — PASS, run via `scratch\build_test.cmd`. `host` = pluginval strictness 5 PASS 2026-08-30 (run by hand on the first build; `bundle` / `deploy` declared, not yet run via validate.ps1). No `gui` stage (D-1). |
| Params | **3** (`masterGain` float 0..1 def 0.72 / `mode` choice AS-AUTHORED,LOOP-ONLY / `midiChannel` choice OMNI,CH 1..16), append-only. Non-param state: `sf2Path`, `presetIndex` in the APVTS tree. |
| EAR-GATE | Everything — no build has been heard. First audition covers acceptance 1–5 of docs/DSP.md in one pass. |
| Engine | Own read-pointer player over TSF's float pool (`Source/engine/ScoutEngine.*`); TSF used for parsing only, plus our own `shdr` walk for sample names/stereo. 32 voices, oldest-steal, 80 ms linear release, sqrt velocity, ±2 st bend, no envelope/LFO/filter/modulators by design. |
| GUI | Plain JUCE fixed 920×522 (D-1, D-3). Handoff v1 (`docs/handoff-gui-v1/`) recreated: header LOAD/filename/mode, preset list + steppers, readout (4 big fields + 9-cell stats + loop bar with live playhead), zone strip (click = audition, hover tooltip) + zone labels, footer master/dB/MIDI combo/voice dot. All controls wired. Fonts substituted (D-2). |
| Git | `main`, initial commit pushed to `mannyzagri/sf2-scout` — see breakpoint for the hash. |
| Pending | **Blockers: operator signature on `SSOT.md`; confirm `PLUGIN_CODE Sf2s` + product string `SF2 Scout` before the first Cubase load.** Follow-ups (NOT blockers): run `validate.ps1` bundle/host stages (pluginval 5); deploy to `Builds\`; first ear pass; decide D-5 (loop crossfade) after hearing it; gui-claude asked for a native `handoff-manifest.json` in v2. Cleanup: `scratch\` is disposable. |

## Next action

1. Operator: answer OPEN QUESTIONS below (one batch).
2. vm-claude: `validate.ps1 -Project C:\sf2-scout` (bundle + host), then
   `release.ps1` to deploy `SF2 Scout.vst3` to `C:\vagrant\Builds\`.
3. Operator ear pass in Cubase 15 with three SF2s (docs/DSP.md acceptance 1–5).

## OPEN QUESTIONS (kickoff interview batch — 2026-08-30)

1. **Sign `SSOT.md`** first entry (or amend the canon table first).
2. **`PLUGIN_CODE Sf2s`** and product string **`SF2 Scout`** — confirm before
   the first Cubase load; both freeze there.
3. Era-math regime recorded as **modern / n.a.** and GUI type as **fixed
   plain-JUCE** (CLAUDE.md §0) — confirm or overrule.
4. Repo `mannyzagri/sf2-scout` visibility — assumed private (fleet default).
5. Window height: D-3 uses the row sum (522 px), not the 700 px preview frame.
   Fine, or do you want the taller frame?
6. Bundle IBM Plex fonts (D-2) or keep the substitution?
7. Standalone target is built alongside the VST3 — keep, or VST3 only?

## RULED (date + user)

*(none yet)*

## Facts worth not re-deriving

- TSF frees the hydra after `tsf_load`; `tsf_region` has no sample name and no
  stereo flag. `SoundFontBank` walks the `shdr` chunk itself (46-byte records)
  and joins regions to headers by `region.offset ∈ [shdr.start, shdr.end)`.
- TSF stores `loop_end` as the INCLUSIVE last index (`shdr.endLoop - 1`);
  `Zone::loopEnd` is exclusive again (SF2 semantics) — `loopEndRel()` prints
  the file's own number.
- A stereo SF2 sample is two `shdr` records and two zones (L/R, pan ±);
  `findZones` returns both and the engine starts one voice each. The readout
  describes the first and says "stereo" from `sampleType ∈ {2,4}`.
- `ScoutEngine::noteOn` consumes a pending bank itself, so a note queued before
  the first `process()` is not dropped (the harness caught this).
- `git ls-remote` on the repo returned empty/exit 0 at kickoff = repo exists and
  is empty (CLAUDE-WORKFLOW.md §1.1).
