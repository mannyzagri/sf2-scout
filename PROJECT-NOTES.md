# SF2 Scout — project notes

Last updated: 2026-09-04 (Slot W pass, vm-claude)

## STATE (overwrite-only; CURRENT state — never append history here)

| Item | Value |
|---|---|
| Deployed | `SF2 Scout.vst3` **v0.1.0** still the deployed build (2026-08-30). **v0.3.0 built on branch `feat/slot-w`, NOT deployed** (0.2.0 was operator-tested: MIDI silent in the exe, markers not draggable → fixed SC-017/019) — the parent session runs release.ps1 / ship.ps1. AWAITING user Cubase verdict on both (first load — `PLUGIN_CODE Sf2s` freezes on it). |
| Validator | 2026-09-04 (local, branch feat/slot-w): `dsp` = 1 harness `test_engine`, **217 checks** in 21 sections (the 12 SF2 sections + wav-load, wav-pitch, wav-forward, wav-pingpong, wav-oneshot, wav-release, wav-focus, wav-smpl, markers) ALL CHECKS PASSED; Release configure+build clean, 0 project warnings; Standalone smoke-launches. `bundle`/`deploy`/`host` (pluginval 5) NOT re-run this pass — release.ps1 does that. No `gui` stage (D-1). |
| Params | **3** (`masterGain` / `mode` / `midiChannel`), append-only, UNCHANGED in 0.2.0/0.3.0. Non-param tree state: `sf2Path`, `presetIndex`, and Slot W: `wavPath`, `wavLoopStart`, `wavLoopEnd`, `wavLoopMode`, `wavRoot`, `wavCents`, `wavAttackMs`, `wavReleaseMs`, `focus`, `splitNote`, `wavExport16Mono`, `wavDescription`, `wavPrefix`, `midiInputDevice` (standalone; "" = all). |
| EAR-GATE | v0.1.0 items (1)–(5) still unheard. v0.2.0 adds: (6) drop JD_STR1_C4.wav → waveform; W focus, hold a key → forward loop sustains; P → ping-pong bounces; Ctrl+S; reload → markers restored; (7) saved file shows identical loop points in Cubase's sample editor (inclusive end); (8) SPLIT: below C4 = SF2, at/above = WAV, both in tune; (9) a Polyphone-tagged WAV shows its markers/type on load; (10) 0.3.0: exe hears the Launchkey with ALL / the chosen device, replug survives; click anywhere on the waveform places the nearer marker and the fields follow, typed values move the cursors; PLAY C3 latches in any focus. |
| Engine | Own read-pointer player over TSF's float pool (`Source/engine/ScoutEngine.*`) + Slot W WAV source (`Source/engine/WavSample.*`) in the SAME 32-voice pool: `setWav`/`takeRetiredWav` handoff mirrors the bank; loop points packed in one 64-bit atomic read per block; fwd wrap via fmod, ping-pong reflection at both markers (hostile-step fold), off = one-shot; root+cents transpose; RELEASE 10–5000 ms / ATTACK 0–500 ms fades on W voices only; SF2 voices keep the fixed 80 ms fade. Focus routing in `noteOn` (`routesToWav`); `noteOnWav` bypasses focus for the PLAY button. Marker math `Source/engine/LoopMarkers.h`. |
| GUI | Plain JUCE fixed 920×796 (was 522; +274 px Slot W band). Header gains FOCUS R/W/SPLIT + split field. Slot W band: LOAD WAV, LOOP FWD/PING-PONG/OFF, ZERO-X SNAP, PLAY C3 (latch), 16-BIT MONO, SAVE AS, SAVE; waveform (wheel zoom, shift/middle-drag pan, left click-to-place + drag nearer marker, ±8 px grab, right-drag = END), seam view, fields START/END/LEN/ROOT/FINE/ATTACK/RELEASE/PREFIX/DESC, status line. Keys: F/P/O, `[ ] { }` start, `; ' : "` end, Ctrl+S, Ctrl+Shift+S. Readout two-column when a WAV is loaded. Footer (standalone only): MIDI input DEVICE chip, ALL by default. SAVE with 16-BIT MONO checked routes to SAVE AS (never degrades the source). Handoff v1 tokens reused; the Slot W band has NO gui-claude handoff yet (function-first per the directive). |
| Git | `main` @ v0.1.0 (pushed). Branch `feat/slot-w` off `docs/wav-extension-directive`: v0.2.0 commit d13ed78 + v0.3.0 commit (hash in the parent's report). Not merged, not pushed by this pass. |
| Pending | **Parent session: release.ps1 (validator full) → deploy → ship; merge feat/slot-w --no-ff.** Operator: ear pass (EAR-GATE 1–9) + Cubase smpl cross-check. Deferred SC-016 (EXPORT RANGE, SUGGEST, CLICK METER, AUTO-DETECT, A/B, kbd piano). Ask gui-claude for a Slot W handoff if the function-first face is to be styled. D-2/D-3/D-5 unchanged. |

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
