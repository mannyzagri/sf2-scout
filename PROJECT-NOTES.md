# SF2 Scout — project notes

Last updated: 2026-08-30 (kickoff, vm-claude)

## STATE (overwrite-only; CURRENT state — never append history here)

| Item | Value |
|---|---|
| Deployed | `SF2 Scout.vst3` **v0.1.0** (kickoff build + architect-review fixes SC-006..008; x86_64 Windows, built 2026-08-30) — at `C:\sf2-scout\SF2 Scout.vst3` + `\VBOXSVR\vagrant\Builds\SF2 Scout.vst3` (release.ps1, SHA-verified); standalone `Builds\SF2 Scout.exe` copied alongside by hand. Binary literal version string `0.1.0`; footer paints `v0.1.0 <date> <time>`. AWAITING user Cubase verdict (first load — `PLUGIN_CODE Sf2s` freezes on it). |
| Validator | 2026-08-30: dsp + bundle + deploy + host ALL PASS via release.ps1. `dsp` = 1 harness `test_engine`, **112 checks** in 12 sections (load, authored, looponly, pitch, release, malformed, swap, hostile-pitch, seqlock, sample-id, stereo-pair, pool-bound), pinned by pin-harness.ps1. `host` = pluginval strictness 5. handoff-lint: 0 mismatches / 4 declared gaps. No `gui` stage (D-1). |
| Params | **3** (`masterGain` float 0..1 def 0.72 / `mode` choice AS-AUTHORED,LOOP-ONLY / `midiChannel` choice OMNI,CH 1..16), append-only. Non-param state: `sf2Path`, `presetIndex` in the APVTS tree. |
| EAR-GATE | v0.1.0 entire — nothing heard yet. Listen for: (1) three SF2s load via LOAD and via drag-drop, list presets, play; (2) AS-AUTHORED held looped note sustains without clicks (D-5: no seam crossfade yet); (3) LOOP-ONLY starts in the sustain, no attack; (4) chromatic run across a zone edge flips SAMPLE/ROOT exactly at the strip's band edge; (5) root-key note at native pitch, ±12 st tracks. |
| Engine | Own read-pointer player over TSF's float pool (`Source/engine/ScoutEngine.*`); TSF used for parsing only, plus our own `shdr` walk for sample names/stereo. 32 voices, oldest-steal, 80 ms linear release, sqrt velocity, ±2 st bend, no envelope/LFO/filter/modulators by design. |
| GUI | Plain JUCE fixed 920×522 (D-1, D-3). Handoff v1 (`docs/handoff-gui-v1/`) recreated: header LOAD/filename/mode, preset list + steppers, readout (4 big fields + 9-cell stats + loop bar with live playhead), zone strip (click = audition, hover tooltip) + zone labels, footer master/dB/MIDI combo/voice dot. All controls wired. Fonts substituted (D-2). |
| Git | `main`, pushed to `mannyzagri/sf2-scout` via ship.ps1 (hash in breakpoint); `-src` export refreshed on the share. |
| Pending | **Blocker: the operator's first Cubase load = ear pass (EAR-GATE row) — it also freezes `Sf2s` / `SF2 Scout`.** Follow-ups (NOT blockers): decide D-5 (loop crossfade) after hearing it; decide D-2 (bundle IBM Plex) and D-3 (window height) from the OPEN QUESTIONS; gui-claude asked for a native `handoff-manifest.json` in v2; architect NIT 12 (O(regions×shdrs) fallback join on hostile pdta) left as-is. Cleanup: `scratch\` is disposable. |

## Next action

1. Operator: ear pass in Cubase 15 with three SF2s (EAR-GATE row) — and
   OPEN QUESTIONS 5–7 whenever convenient.
2. vm-claude: fold the verdict; D-5 crossfade only if the seam clicks.

## OPEN QUESTIONS (kickoff interview batch — 2026-08-30)

1. ~~Sign `SSOT.md`~~ — RULED.
2. ~~`PLUGIN_CODE Sf2s` / `SF2 Scout`~~ — RULED ("confirmed").
3. ~~Era-math / GUI type~~ — RULED ("confirmed").
4. Repo `mannyzagri/sf2-scout` visibility — assumed private (fleet default).
5. Window height: D-3 uses the row sum (522 px), not the 700 px preview frame.
   Fine, or do you want the taller frame?
6. Bundle IBM Plex fonts (D-2) or keep the substitution?
7. Standalone target is built alongside the VST3 — keep, or VST3 only?

## RULED (date + user)

- 2026-08-30, Menashe Zagri: SSOT.md first entry signed; kickoff declarations (era n.a., fixed plain-JUCE, `Sf2s` / `SF2 Scout`) confirmed; SF2 must load via drag-and-drop AND the LOAD button (both implemented — `ScoutEditor::filesDropped` / `chooseFile`); builds deploy to the share `Builds\`.

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
