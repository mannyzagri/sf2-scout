# SF2 Scout — project notes

Last updated: 2026-09-12 (v2 build order 1, vm-claude)

## STATE (overwrite-only; CURRENT state — never append history here)

| Item | Value |
|---|---|
| Deployed | `SF2 Scout.vst3` **v0.1.0** still the deployed build (2026-08-30). **v0.5.0 built on branch `feat/v2`** (off `feat/slot-w` — 0.2.0–0.4.0 never merged, never ear-verdicted). Deploy/ship decision: this session's report. AWAITING user Cubase verdict on 0.2.0–0.5.0 (first load — `PLUGIN_CODE Sf2s` freezes on it). |
| Validator | 2026-09-12 (local, branch feat/v2): `dsp` = harness `test_engine` (CMake target, links `openmpt_soundlib`), **284 checks** in 25 sections (the 23 of 0.4.0 + mod-load, mod-play) ALL CHECKS PASSED; Release configure+build clean, 0 project warnings (2 upstream libopenmpt warnings ignored); Standalone smoke-launches; `--probe` verified on real MOD / XM / IT files (scratch/modules, untracked). `bundle`/`deploy`/`host` (pluginval 5) NOT re-run this pass. No `gui` stage (D-1). |
| Params | **3** (`masterGain` / `mode` / `midiChannel`), append-only, UNCHANGED since 0.1.0. Non-param tree state: `sf2Path`, `presetIndex`, Slot W: `wavPath`, `wavLoopStart`, `wavLoopEnd`, `wavLoopMode`, `wavRoot`, `wavCents`, `wavAttackMs`, `wavReleaseMs`, `focus`, `splitNote`, `wavExport16Mono`, `wavDescription`, `wavPrefix`, `midiInputDevice`; v2: `modulePath`, `moduleSample` (1-based; restored before `wavPath`). |
| EAR-GATE | v0.1.0 items (1)–(5) still unheard. v0.2.0 adds: (6) drop JD_STR1_C4.wav → waveform; W focus, hold a key → forward loop sustains; P → ping-pong bounces; Ctrl+S; reload → markers restored; (7) saved file shows identical loop points in Cubase's sample editor (inclusive end); (8) SPLIT: below C4 = SF2, at/above = WAV, both in tune; (9) a Polyphone-tagged WAV shows its markers/type on load; (10) 0.3.0: exe hears the Launchkey with ALL / the chosen device, replug survives; click anywhere on the waveform places the nearer marker and the fields follow, typed values move the cursors; PLAY C3 latches in any focus; (11) 0.4.0: KEYBOARD PLAYS SF2/WAV/SPLIT greys out empty slots and the keyboard never goes silent on an empty choice; UNLOAD on either slot stops its voices and clears its section, the other slot keeps sounding; (12) 0.5.0 / v2 acceptance 1: drop a .mod, an .xm and an .it with ping-pong samples → Slot W shows `file ▸ NN name ▾`, click it / press `,` `.` to step samples, each plays in tune on C4 (middle C = the tracker's C-5) and its own loop sustains (ping-pong bounces); SAVE AS exports a WAV whose loop points Cubase shows; **→ W** on an SF2 preset decodes the readout's zone into Slot W and SAVE AS exports it with the zone's loop; junk files give an error and keep the previous state. |
| Engine | Own read-pointer player over TSF's float pool (`Source/engine/ScoutEngine.*`) + Slot W WAV source (`Source/engine/WavSample.*`) in the SAME 32-voice pool (see 0.4.0 notes in CHANGELOG for the handoff/loop/fade contract). **v2 bridge**: every non-WAV sample enters Slot W as a decoded `WavSample` — `ModuleSource::decode` (libopenmpt soundlib, `Source/engine/ModuleSource.*`; 8→16-bit; module loop or IT sustain loop, inclusive end; tuning = C-5 Hz as the WAV rate, root 60) and `SoundFontBank::decodeZone` (float pool → 16-bit; zone loop; root − coarseTune, fine cents); `WavSample::fromPcm16` synthesises fmt+data so `writeWav` exports it unchanged. Containers are never written. |
| GUI | Plain JUCE fixed 920×796 (unchanged geometry). v2 additions: LOAD / LOAD WAV / drag-and-drop accept every libopenmpt extension (→ Slot W); Slot W title `module ▸ NN name ▾` is a click target for the sample chooser popup (index, name, bits, frames, loop kind, C-5 Hz); `,` `.` step samples; **→ W** button in the preset-list header (SF2 zone → Slot W, asks if Slot W is dirty); SAVE on a decoded sample routes to SAVE AS; status line shows format / title / sample count on module load. Everything from 0.2.0–0.4.0 unchanged. No gui-claude handoff for the Slot W band or the v2 controls yet (function-first). |
| Git | `main` @ v0.1.0 (pushed). `feat/slot-w` = 0.2.0–0.4.0 (808696b, unmerged). `feat/v2` off feat/slot-w = 0.5.0 (this session; hash in breakpoint.md). |
| Pending | Operator: ear pass EAR-GATE (1)–(12) + Cubase smpl cross-check; sign the SSOT amendment (v2 canon); rule on OPEN QUESTIONS 8–10. vm-claude next: v2 build order 3 extras (EXPORT RANGE, BATCH export of a whole module / preset, native-vs-16-bit + stereo-fold options) then 4 (A/B slots, SUGGEST / CLICK METER / LOUDNESS, metadata boxes, SOURCE LIST panel) — SC-016 carries the assists. Deferred: SF2 stereo pairs export as two mono halves (decodeZone); libopenmpt MO3/vorbis samples need zlib/vorbis (not vendored). |

## Next action

1. Operator: ear pass EAR-GATE (1)–(12) in Cubase 15 / standalone with the
   Launchkey; sign SSOT amendment 2026-09-12; OPEN QUESTIONS 8–10.
2. vm-claude: fold the verdict, then v2 build order 3 (EXPORT RANGE, BATCH).

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
10. SF2 stereo pairs: `→ W` decodes ONE half (the zone under the readout);
    the pair exports as two mono WAVs (L, R). Join into one stereo WAV instead?

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
