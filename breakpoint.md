# breakpoint — sf2-scout (overwrite-only; subordinate to PROJECT-NOTES.md STATE)

**Session end: 2026-09-12, vm-claude — v2 build order 1 (0.5.0).**

## Where things stand

- The operator's `SCOUT_v2_SPEC.md` (share drop, 2026-09-12) is ingested as
  `docs/SCOUT_v2_SPEC.md` and is the behaviour canon (CLAUDE.md scope layer;
  SSOT amendment **UNSIGNED**). Export is a feature; containers stay read-only.
- libopenmpt 0.8.9 soundlib vendored (`third_party/libopenmpt/`, BSD-3) and
  compiled as `openmpt_soundlib`. `ModuleSource` decodes any tracker sample
  into a `WavSample`; `SoundFontBank::decodeZone` does the same for an SF2
  zone; both land in Slot W and export through the existing writer.
- Harness = CMake target `test_engine`, **284 checks** green, `--probe` mode
  verified on real MOD / XM / IT files (`scratch/modules/`, untracked).
- Release build clean, standalone smoke-launched. **Not deployed, not merged**
  — 0.2.0 through 0.5.0 all wait on the operator's ear verdict (STATE).
- The share's `sf2-scout-src/` is the 2026-08-30 v0.1.0 snapshot (byte-identical
  to commit dc1ab5a): it is NOT newer than the repo. `validator.json srcExport`
  refreshes it on ship.

## Fresh-session entry ramp

1. `git pull` (mac lane not open; still the rule). Branch `feat/v2`.
2. Read CLAUDE.md §"Scope layer 2026-09-12", PROJECT-NOTES STATE, this file,
   `comms\to-vm.md`, and `docs/SCOUT_v2_SPEC.md`.
3. Fold the ear verdict (EAR-GATE 1–12). Then v2 build order 3: EXPORT RANGE
   and BATCH export (whole module / whole preset, `<SOURCE>_<name>_<NOTE>.wav`,
   skip-existing) — the bridge functions already give every sample as a
   `WavSample`, so batch = loop over `decode`/`decodeZone` + `writeWav`.
4. Any change: harness (`cmake --build build --config Release --target test_engine`)
   → `release.ps1 -DryRun` → `release.ps1` → `ship.ps1`.

## Gotchas learned this session

- libopenmpt's PUBLIC API has no sample PCM / loop flags; read
  `OpenMPT::CSoundFile` + `ModSample` directly with `LIBOPENMPT_BUILD` defined.
  `AssertHandler` must be provided by us (no-op in ModuleSource.cpp).
- `ModSample::GetSampleRate(type)` already folds MOD/XM finetune + relative
  note into Hz (and the PAL 8287 Hz for MOD); IT/S3M give nC5Speed.
- libopenmpt loop ends are EXCLUSIVE; `WavSample` wants INCLUSIVE.
- Perl `s|a|b|` with `\|` inside the pattern corrupts the file on this VM —
  use another delimiter (`s{}{}`) for edits containing `||`.

## Git

- `main` @ v0.1.0 (pushed). `feat/slot-w` @ 808696b (0.2.0–0.4.0, unmerged).
- `feat/v2` = this session's 0.5.0 commit (see `git log -1`), pushed.

## Artefacts of this session

- `build\Sf2Scout_artefacts\Release\VST3\SF2 Scout.vst3`, `...\Standalone\SF2 Scout.exe`
- `build\Release\test_engine.exe` (harness + `--probe`)
- `scratch\modules\` — three public-domain test modules from modarchive.org
  (fading_horizon.mod, eternity.xm, fall_in_love.it) + exported WAVs in `out\`
