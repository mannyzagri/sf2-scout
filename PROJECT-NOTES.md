# SF2 Scout — project notes

Last updated: 2026-09-13 (GUI handoff v2 landed = 0.6.0, vm-claude)

## STATE (overwrite-only; CURRENT state — never append history here)

| Item | Value |
|---|---|
| Deployed | **v0.6.0 DEPLOYED 2026-09-13 22:55** via release.ps1 (bundle fresh/structure PASS, pluginval 5 SUCCESS) to `C:\sf2-scout\SF2 Scout.vst3` and `\\VBOXSVR\vagrant\Builds\SF2 Scout.vst3`; `SF2 Scout.exe` 0.6.0 copied to the share `Builds\` by hand (the tool deploys the VST3 only). Branch `feat/v2` (0.2.0–0.6.0 unmerged; `main` = 0.1.0). Cubase re-scan REQUIRED, header stamp must read 0.6.0, remove/re-add the instance. AWAITING the operator's FIRST LOOK at the v2 face + ear pass (first Cubase load — `PLUGIN_CODE Sf2s` freezes on it). |
| Validator | 2026-09-13: release.ps1 = bundle PASS, deploy ×2 PASS, host pluginval 5 SUCCESS; `dsp` = `test_engine` **347 checks** in 27 sections ALL CHECKS PASSED (`validator.json` expect list updated); handoff-lint on `docs/handoff-gui-v2/handoff-manifest.json` = 2 unruled mismatches, both manifest SHAPE (61 widgets under `controls`; `master` / `auditionMode` renames of `masterGain` / `mode`) — re-emit asked, contract ids kept (OPEN QUESTION 14). No `gui` stage (D-1). Standalone smoke 2026-09-13: launched; a session with two modules + a WAV restored; PLAY, EXPORT SAMPLE, ›, SUGGEST, AUTO-DETECT ROOT, EXPORT ALL exercised through UI Automation; exported WAV chunks verified (fmt 29802 Hz 16-bit mono, smpl unity 60 loop 13171–19895 type 1, bext Originator "Scout v2" + date/time). |
| Params | **3** (`masterGain` / `mode` / `midiChannel`), append-only, UNCHANGED since 0.1.0. Non-param tree state (0.6.0 layout, SC-038): children `sources` (path, selPreset, expanded — order matters) and `edits` (per sample key `srcIndex:a:b`: loopStart, loopEnd, loopKind, loopOverride, root, cents, attackMs, releaseMs, prefix, description); properties `selSrc`, `cur`, `slotA`, `slotB`, `kbMode`, `splitNote`, `toggleOn`, `exportFolder`, `exportQuick`, `exportConvert16`, `exportFold`, `exportScope`, `exportSkip`, `midiInputDevice`, `uiScale`. A 0.5.0 session restores its params only. |
| EAR-GATE | Items (1)–(12) of 0.1.0–0.5.0 still unheard (now through the v2 face). 0.6.0 adds: (13) LOAD / drop several sources → SOURCE LIST rows with type badges, CONTENTS per source; a click selects the sample for the editor, double-click / SPACE plays it at its root; a junk file becomes a red error row, nothing else changes; (14) → A / → B, KEYBOARD PLAYS A / B / SPLIT (split note field) / TOGGLE (TAB swaps), readout A | B follows, live dot marks the reachable slot; (15) zone map follows the slot holding an SF2 zone, click plays that zone's sample; (16) editor: drag markers (snap to a rising zero crossing on release), F/P/O, `[ ] { }` `; ' : "`, SUGGEST moves END to a clean seam, AUTO-DETECT ROOT sets root + cents, PERIODS tints when non-integer, CLICK METER / LOUDNESS read; (17) EXPORT SAMPLE / EXPORT RANGE / EXPORT ALL write into the chosen folder ("reference" / "rom" remembered across instances), NATIVE vs 16b/44.1 MONO, SUM / L ONLY; the files open in Cubase with the loop points shown (acceptance 3–6); (18) SAVE on a WAV source round-trips, SAVE AS names `<PREFIX>_<NOTE>.wav`; (19) the window resizes with a fixed aspect and the face stays crisp (bundled Plex); (20) QWERTY Z–M plays through the keyboard routing. |
| Engine | Three-slot decoded-sample player (`ScoutEngine`: A, B, Cur; `routeSlot` A/B/SPLIT/TOGGLE with empty-slot fallback), **16 voices** (D-10), per-slot loop / tuning / fades / playhead / last note, LOOP-ONLY starts inside the loop; slot handoff = one retiree per slot, never-played pending samples handed back, `forgetAll` for shared owners. `Assists` (loudness, periods, click ratio, suggest, auto-root), `exportWav` (range + remap, 16/44.1 mono sinc + TPDF, SUM / L-only), `SoundFontBank::info()`. Decoders unchanged (`decodeZone`, `ModuleSource::decode`, `fromPcm16`); the SF2 key-range player is gone (D-9). Containers are never written. |
| GUI | v2 face verbatim from `docs/handoff-gui-v2/` (SC-039): 1200 × 840 canvas scaled 0.75–1.5 (OPEN QUESTION 11), Plex bundled (SC-040). Files: `Source/ui/{Fonts,Palette,Widgets,Lists,Readout,WaveEditor,ExportColumn}.h`, assembly + wiring `Source/PluginEditor.cpp`. Handoff gaps reported to gui-claude: manifest shape + 2 param-id renames; deviceChip x contradicts the README flex rule (built per the rule, D-8). |
| Git | `main` @ v0.1.0 (pushed). `feat/slot-w` = 0.2.0–0.4.0 (808696b, unmerged). `feat/v2` = 0.5.0 (11be137) + handoff v2 landing (9ffce4d) + 0.6.0 (this session; hash in breakpoint.md). |
| Pending | Operator: first look at the v2 face + ear pass (13)–(20) + Cubase smpl cross-check of exports; rule OPEN QUESTIONS 8–14; sign the SSOT amendment (v2 canon). gui-claude: manifest re-emit (schema in the mailbox). vm-claude next: fold the operator's defect list; then merge `feat/v2` → `main` (--no-ff) once the ear verdict is in. Deferred: libopenmpt MO3/vorbis samples need zlib/vorbis (not vendored); SF2 stereo pairs as one stereo file (OQ 10). |

## Next action

1. Operator: re-scan, first look at 0.6.0 (the v2 face), ear pass EAR-GATE
   (13)–(20) + the old (1)–(12); Cubase cross-check of one EXPORT SAMPLE, one
   EXPORT RANGE and one BATCH file; rule OPEN QUESTIONS 8–14.
2. vm-claude: fold the verdict / defect list, then `feat/v2` → `main`.

## OPEN QUESTIONS (kickoff interview batch — 2026-08-30)

1. ~~Sign `SSOT.md`~~ — RULED.
2. ~~`PLUGIN_CODE Sf2s` / `SF2 Scout`~~ — RULED ("confirmed").
3. ~~Era-math / GUI type~~ — RULED ("confirmed").
4. Repo `mannyzagri/sf2-scout` visibility — assumed private (fleet default).
5. Window height: D-3 uses the row sum (522 px), not the 700 px preview frame.
   Fine, or do you want the taller frame?
6. Bundle IBM Plex fonts (D-2) or keep the substitution?
7. Standalone target is built alongside the VST3 — keep, or VST3 only?

### v2 batch (2026-09-12)

8. Tuning convention for decoded module samples: the WAV is written at the
   tracker's C-5 frequency (e.g. 8287 Hz for a finetune-0 MOD) with root 60,
   so it plays in tune on middle C and the tuning survives in `fmt`. Cubase
   will show odd sample rates. Alternative: resample to 44.1k and fold the
   tuning into root/cents (lossy). Keep as is?
9. IT sustain loops: when a sample has BOTH a sustain loop and a normal loop,
   the decoded sample carries the SUSTAIN loop (what sounds while held). OK?
10. SF2 stereo pairs: a zone decodes as ONE half; the pair exports as two mono
    WAVs (L, R). Join into one stereo WAV instead?

### handoff v2 batch (2026-09-13) — built under these assumptions, reversible

11. Window: handoff v2 makes the face a 1200 × 840 canvas scaled 0.75–1.5
    (resizable, fixed aspect). The kickoff declaration said "fixed, not
    resizable". Built as the handoff says. Keep scalable, or lock at 1× ?
12. Voices: v2 spec says 16, the v1 engine had 32. Built 16 (`kMaxVoices`).
13. Stereo sources export MONO (SUM / L ONLY per the STEREO option); native
    keeps rate + depth. Stereo exports wanted instead? (D-7)
14. Manifest ids: gui-claude's `params` renamed `masterGain`→`master` and
    `mode`→`auditionMode`. Built on the frozen contract ids (hosts save by
    name); re-emit asked. Confirm the contract stays as is.

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
