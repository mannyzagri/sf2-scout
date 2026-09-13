<!-- ======================================================================
     STANDARD PREAMBLE (code-bank/templates/CLAUDE-header.md)
     Identical across projects. Project-specific content starts below it.
     ====================================================================== -->

# Session bootstrap (do these IN ORDER before any task)

0. **Know who and where you are.** On this VM you are **vm-claude** — DSP
   coder, architect, plugin builder; you own the repo, the build, the
   validator, and git. GUI design belongs to **gui-claude** (claude.ai
   design track), R&D to **mac-claude**, hardware/DAW testing to
   **windows-claude** (winhost) — roster + lanes: CLAUDE-WORKFLOW.md §0
   (SSOT). The share `C:\vagrant` (= `\\VBOXSVR\vagrant`): deploys →
   `Builds\`, per-project material + incoming GUI handoff zips →
   `Projects\<project>\`, LLM↔LLM mailbox → `mailbox\<project>\` — full
   map: CLAUDE-WORKFLOW.md §3. **Low-chatter is the default during any
   pipeline**: one TLDR line per decision, one report at the end; interrupt
   the human only when blocked / contradiction / correction on offer
   (CLAUDE-WORKFLOW.md §0).
1. **Skills** — house skills are junction-linked in `.claude\skills\`
   (source: `C:\code-bank\skills\`). If missing from your skill list, read
   the SKILL.md and follow it. Never hand-roll a pass a skill covers
   (dsp-pass, gui-pass save tokens by design — use them).
2. **Agents** — personas in `.claude\agents\` (canonical:
   `C:\code-bank\agents\`): `juce-dsp` (audio thread, DSP, processor,
   CMake), `juce-webview` (editor C++ bridge + frontend JS/HTML/CSS),
   `architect-reviewer` (design review). Route builds and tests through
   `build-runner`, and broad code search through `Explore` — both return
   summaries only, by contract. `cpp-pro` / `frontend-developer` are
   situational fallbacks, not the default for plugin work. Registration
   check + invocation fallback: CLAUDE-WORKFLOW.md §6 (SSOT — not restated
   here).
3. **Shared workflow** — read `C:\code-bank\CLAUDE-WORKFLOW.md`
   (share rules, validator, release/ship tools, cross-project policy).
   **That path only** — canonical and git-tracked since 2026-08-05; the
   `\\VBOXSVR\vagrant\` mirror is retired and the `C:\Users\vagrant\`
   profile copy demoted. All three have drifted; do not consult them.
4. **Project context** — read the rest of THIS file, then PROJECT-NOTES.md
   **STATE** (the only current-state authority), then the breakpoint file,
   then `comms\to-vm.md` (`[NEW]` messages = your task queue).

House rules that apply here as everywhere: CHANGELOG per
`C:\code-bank\templates\CHANGELOG-convention.md` (numbered, tracked items);
archiving per `C:\code-bank\templates\ARCHIVE-convention.md`; validator via
`C:\code-bank\validator\validate.ps1 -Project <this project>`; branch →
merge --no-ff → push; explicit `git add` lists, never `git add -A`.

<!-- ====================== project-specific below ======================== -->

# SF2 Scout — vintage sample player / editor / converter (VST3 + Standalone)

## Scope layer — 2026-09-12 (user directive): **v2 = universal source player + exporter**

**Read `docs/SCOUT_v2_SPEC.md` first — it is the behaviour canon and it
SUPERSEDES `docs/DSP.md`, `docs/SF2SCOUT_WAV_EXTENSION.md` and
`docs/LOOP_BENCH_SPEC.md`** (kept verbatim for history; where they conflict
with v2, v2 wins). What changed:

- **Sources**: WAV (JUCE-free own reader), SoundFont (TinySoundFont parse, own
  playback), and **tracker modules** (.mod .xm .it .s3m + every libopenmpt
  format) via the vendored **libopenmpt 0.8.9 soundlib** (BSD-3,
  `third_party/libopenmpt/`, compiled as the static lib `openmpt_soundlib`).
  Schism Tracker source is GPL — reference reading only, never copied.
- **Export is a first-class feature.** The old "no export / no save" rule is
  RETIRED. Any sample from any source may be exported as a loop-tagged WAV
  (`smpl` inclusive end + `bext` provenance). What stays absolute: **SF2 and
  module files are read-only containers — never written.** Only WAV files are
  ever written, and Slot W's SAVE (round-trip into the loaded WAV) is refused
  for a decoded sample (it routes to SAVE AS).
- **Bridge model (0.5.0)**: a tracker sample or an SF2 zone is DECODED into a
  `WavSample` (`ModuleSource::decode`, `SoundFontBank::decodeZone`,
  `WavSample::fromPcm16`) and installed in Slot W, so playback, the loop
  editor and the exporter are the same code path for every source. The
  module's own loop (fwd / ping-pong; IT sustain loop preferred while held)
  seeds the markers; tuning travels as the WAV's sample rate (C-5 frequency)
  with root 60 / 0 cents.
- **Build order (spec)**: 1 sources + playback ← **0.5.0 lands this** (module
  load, sample chooser, SF2 zone → W, export via SAVE AS) · 2 editor ·
  3 export extras (EXPORT RANGE, BATCH, native-vs-16-bit options) · 4 A/B
  slots, assists, metadata boxes, SOURCE LIST panel, polish.

## Scope layer — 2026-09-03 (user directive): WAV loop editing = Slot W (folded into v2)

`docs/SF2SCOUT_WAV_EXTENSION.md` (received verbatim) adds a second source
slot: load the user's own hardware-recorded WAVs, set/audition loop points
(forward + ping-pong), save them back as a standard `smpl` chunk (loopStart,
loopEnd, type, dwMIDIUnityNote, pitch fraction) + bext provenance. Its §1–§5
requirements are `docs/LOOP_BENCH_SPEC.md` (the 2026-09-03 revision; the
standalone "Loop Bench" app is cancelled). Output WAVs feed The Dreamer's ROM
ingest (`C:	he-dreamer\ROM_REPLACEMENT.md`). The no-SF2-export rule stays
ABSOLUTE: only Slot W can ever be saved. Focus switch R / W / SPLIT (default
C4). Where this layer conflicts with an earlier phase plan, this layer wins.

## §0 Kickoff declarations (2026-08-30, vm-claude — operator to confirm; see PROJECT-NOTES OPEN QUESTIONS)

| Declaration | Value | Why |
|---|---|---|
| Era-math regime | **modern / n.a.** — the tool is a sample read-pointer with linear interpolation; no synthesis layer exists to be era-faithful about. `vintage-dsp` does NOT apply. | docs/DSP.md "What to IGNORE" |
| GUI type | **fixed layout, scalable window** (since handoff v2, 2026-09-13): a 1200 × 840 plain-JUCE canvas, scaled uniformly 0.75–1.5 with a fixed aspect (`setResizable` + `setFixedAspectRatio` + `AffineTransform::scale`). The kickoff "fixed, not resizable" declaration is superseded by the handoff; the operator has not ruled on it (OPEN QUESTION 11). | handoff v2 README "CHANGES FROM V1" 1 |
| GUI technology | **plain JUCE Components — NOT the house WebView pattern.** Recorded deviation D-1. | docs/DSP.md "Tech approach": *"No editor framework beyond stock JUCE components … WebView GUI NOT required"*; handoff README "About the Design Files" says the same |
| JUCE acquisition | FetchContent pinned to 8.0.4, satisfied offline by `-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/rhino/deps/JUCE` (rhino pattern). | no network dependency at configure |
| SF2 parsing | **TinySoundFont v0.9** (MIT), vendored at `third_party/tsf/tsf.h` + `LICENSE` with a 3-line marked patch (D-6). Used for parsing only; playback is our own (spec "Tech approach"). | docs/DSP.md |
| Roles used | vm-claude (everything in the repo), gui-claude (the face — handoff v1 received 2026-08-29), windows-claude (Cubase 15 ear test on the host). mac-claude not used yet. | `comms/AGENTS.md` |
| Repo | `mannyzagri/sf2-scout` — exists, private assumed (created empty by the operator before kickoff). | `git ls-remote` empty/exit 0 on 2026-08-30 |

## Naming contract (WORKFLOW §7 rule 9 — recorded at kickoff, before first build)

| Slot | Value | Freezes when |
|---|---|---|
| Repo / codename | `sf2-scout` (`mannyzagri/sf2-scout`, `C:\sf2-scout`) | in use |
| CMake target | `Sf2Scout` | changeable any time |
| Host-visible product string | `SF2 Scout` | **first Cubase load** |
| `PLUGIN_CODE` | `Sf2s` — **PROPOSED, operator to confirm.** Unique in the fleet (Rrh3, Drmr, Dd9t, Bd8t/Bd8r, Qsr1). Manufacturer `Mnsh`. | **first Cubase load** |
| Bundle filename | `SF2 Scout.vst3` (JUCE derives it from PRODUCT_NAME) | changeable, costs a re-scan |
| Standalone | `SF2 Scout.exe` (JUCE Standalone format — "if trivial" per spec; it is) | — |
| Version | `project(Sf2Scout VERSION x.y.z)` in CMakeLists.txt is the single version literal; the binary carries it as `SF2SCOUT_VERSION_STRING` and paints it in the footer | — |

## §1 Doc authority

| Doc | Authoritative for |
|---|---|
| `SSOT.md` | which file is canon per fact domain (unsigned until the operator signs) |
| `docs/SCOUT_v2_SPEC.md` | **behaviour canon since 2026-09-12**: sources, playback, editor, export, acceptance, build order. Received verbatim from the operator. |
| `docs/DSP.md` | SUPERSEDED by v2 (kept verbatim: the 2026-08-29 auditioner spec). Still the reference for SF2-side details v2 does not restate (play modes, what to ignore from the SF2 spec). |
| `docs/handoff-gui-v2/README.md` | appearance since 0.6.0: every colour, size, font, spacing, state and interaction of the v2 face (1200 × 840). Verbatim from the gui-claude bundle `Plugin GUI.zip` (2026-09-13); `SF2 Scout v2.dc.html` is its prototype (design of record for interactions), `handoff-manifest.json` its native manifest (shape re-emit asked — mailbox `VM-HANDOFF-V2-LINT-20260913.md`). `docs/handoff-gui-v1/` is history. |
| `PROJECT-NOTES.md` STATE | current state — deployed build, validator, params, pending |
| `breakpoint.md` | session-end snapshot, subordinate to STATE |
| `CHANGELOG.md` | history; item IDs `SC-nnn` |
| `validator.json` (repo root) | validator matrix; `code-bank/validator/configs/` gets dated exports only |
| `FEATURE-INDEX.json` | feature → file/symbol map; check before grepping |
| `comms/AGENTS.md` | which roles this project uses |

## §2 Non-negotiable rules

1. **Containers are read-only** (docs/SCOUT_v2_SPEC.md): SF2 and module files
   are never written, no preset/pattern editing, no hosting inside The Dreamer.
   Export (WAV out of any source) IS a feature since v2 — the writer is
   `writeWav` and it only ever serialises a `WavSample`. `.gitignore` refuses
   `*.sf2` and module files; test modules live in `scratch/modules/` (untracked).
2. **Parameter IDs are a public API.** `masterGain`, `mode`, `midiChannel` —
   append-only, never rename/reorder (`Source/PluginProcessor.h` `ParamId`).
   Encoding at the decoder: float normalised 0..1; choice = int index.
3. **Audio thread**: no allocation/locks/logging. Every sample reaches the
   engine as a decoded `WavSample` in one of three SLOTS (A, B, Cur); a slot
   swap is an atomic pointer handoff (`ScoutEngine::setSlot` /
   `takeRetired`), the audio thread never frees, and a sample it never
   played is handed back to the caller. The processor keeps shared samples
   alive (`engineHeld_`) until every slot retired them. UI audition notes
   travel through an `AbstractFifo`.
4. **The engine is JUCE-free** (`Source/engine/`, incl. `Assists`) and is
   proven by the harness `tests/test_engine.cpp` BEFORE any plugin build
   (347 checks, 27 sections at 0.6.0). It builds a structurally complete
   SF2 / WAV / MOD in memory, so no third-party material is ever needed or
   committed.
5. **GUI ROLE BOUNDARY (CLAUDE-WORKFLOW.md §4.0)** applies unchanged even
   though the face is plain JUCE: the handoff README is the design; C++ only
   recreates it with the given numbers and wires it. Anything the handoff does
   not specify is a question to gui-claude via the mailbox, not a local
   invention. Numbers the handoff DOES give are copied, not tuned.
6. **Malformed input never crashes** — every loader path returns an error
   string and keeps the previous state (acceptance test 6). Harness section
   `[malformed]` pins it.

## §3 Build / test / validate

```
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/rhino/deps/JUCE
cmake --build build --config Release --parallel
cmake --build build --config Release --target test_engine && build\Release\test_engine.exe   # harness -> ALL CHECKS PASSED
build\Release\test_engine.exe --probe <module> [outDir]      # real-file diagnostic: sample table (+ export every sample)
powershell -ExecutionPolicy Bypass -File C:\code-bank\validator\validate.ps1 -Project C:\sf2-scout
```
The harness is a CMake target since 0.5.0 (it links `openmpt_soundlib`); it is
still JUCE-free. First configure compiles ~150 soundlib TUs once (~3 min).
Artefacts: `build\Sf2Scout_artefacts\Release\VST3\SF2 Scout.vst3`,
`build\Sf2Scout_artefacts\Release\Standalone\SF2 Scout.exe`.
Deploy targets: `validator.json` `deploy.targets`. Compiles go through
`build-runner`.

## §4 Phase plan (docs/DSP.md "Build order") — every phase has a gate

| Phase | Scope | Gate | Status |
|---|---|---|---|
| 0 | Scaffold, SSOT draft, naming, harness, first build | harness ALL CHECKS PASSED; VST3 + Standalone build; pushed | **2026-08-30** — see STATE |
| 1 | Load + preset list + AS-AUTHORED playback | acceptance 1 (three SF2s load, list, play) — operator ear | code complete, unaudited |
| 2 | Own read-pointer path: two modes, release fade, 32-voice pool | acceptance 2, 3, 5 — harness `[authored]` `[looponly]` `[pitch]` `[release]` + operator ear | harness green (112 checks incl. hostile-pitch/seqlock/sample-id/stereo-pair/pool-bound), unaudited |
| 3 | Info readout + zone piano strip | acceptance 4 — chromatic scale across a zone boundary flips the readout at the boundary key | code complete, unaudited |
| 4 | Drag-drop, polish, pluginval 5, Cubase 15 | acceptance 6, 7 | pluginval 5 PASS 2026-08-30 |

v2 build order (docs/SCOUT_v2_SPEC.md "Build order") — supersedes the table above for new work:

| v2 step | Scope | Gate | Status |
|---|---|---|---|
| 1 | Sources + playback: WAV / SF2 / module load, sample list, MIDI play with own loops | v2 acceptance 1 (GM SF2, .it with ping-pong, .xm, .mod, 32f WAV all list, play in tune, honour loops, no crash on junk) | **0.6.0**: several sources open at once (SOURCE LIST + CONTENTS), every sample plays decoded (`[routing]` `[mod-play]`); operator ear pending |
| 2 | Editor (waveform, markers, nudge, seam) + loop modes + fades | acceptance 2, 4 | **0.6.0**: per-sample session edits for any source; snap on release; harness `[markers]` |
| 3 | Export: smpl/bext writer ✔, EXPORT RANGE, BATCH, native/16-bit options, Cubase validation | acceptance 3, 5, 6 | **0.6.0 code complete**: `exportWav` (range + marker remap, 16-bit/44.1 mono sinc + TPDF, SUM / L-only fold), EXPORT SAMPLE / RANGE / ALL with progress + log, folders remembered; harness `[export]`; Cubase validation pending |
| 4 | A/B slots, SUGGEST / CLICK METER / LOUDNESS, metadata boxes, zone map ✔, SOURCE LIST panel, polish | acceptance 7, 8 | **0.6.0 code complete**: A/B/SPLIT/TOGGLE, assists (`[assists]`), METADATA box (SF2 INFO / MODULE INFO / WAV CHUNKS), QWERTY piano; pluginval + ear pending |

## §5 Test material

None committed (rule 1). The harness fabricates its own SF2. For ear tests the
operator supplies three files on the host (GM bank, single-instrument bank,
synth bank — docs/DSP.md acceptance 1); they live on the share, never in git.

## §6 Recorded deviations

| # | Deviation | Why | Date |
|---|---|---|---|
| D-1 | Plain JUCE Components instead of the house JUCE-8-WebView face; validator `gui` stage (headless Chrome + fake bridge) does not apply and is omitted, not faked. | Spec + handoff both mandate stock JUCE; a bench tool built in a day. `juce-webview` persona is therefore situational here, not mandatory. | 2026-08-30 |
| D-2 | ~~IBM Plex Sans/Mono not bundled~~ — **RETIRED 0.6.0**: Plex Sans 400/500/600 + Mono 400/500/700 are bundled from `third_party/fonts/` (OFL 1.1) as BinaryData, per handoff v2. | Handoff v2 CHANGES 2. | 2026-09-13 |
| D-3 | ~~Window height = row sum 522 px~~ — **SUPERSEDED 0.6.0** by the v2 canvas 1200 × 840 (handoff v2 states it explicitly). | — | 2026-09-13 |
| D-7 | A stereo source (stereo WAV, stereo module sample) EXPORTS as mono, folded per the STEREO option (SUM −3 dB / L ONLY); native export keeps rate and depth only. SAVE (WAV round-trip) stays byte-identical stereo. | Spec lists "stereo fold rules (sum, or L-only) selectable" and the face enables the option for any stereo source; the Dreamer ROM takes mono. Reversible if the operator wants stereo exports (OPEN QUESTION 13). | 2026-09-13 |
| D-8 | Footer DEVICE chip at x 772 (manifest said 806): the manifest's resolved x would overlap the "MIDI IN" label; README §6's flex rule (gap 12, right-anchored) wins, reported to gui-claude. | Handoff-internal contradiction; the README prose is the design of record. | 2026-09-13 |
| D-9 | The engine no longer has an SF2 preset-by-key-range player: every sample (SF2 zone, module sample, WAV) plays as a decoded `WavSample` in a slot, so session edits are what you hear. The zone map click selects and plays that zone. Stereo SF2 pairs play/export as two mono halves (OPEN QUESTION 10). | v2 spec "note-on plays the selected sample transposed from its root"; A/B are single samples. | 2026-09-13 |
| D-10 | Voice pool 16 (spec) — was 32 in v1 (docs/DSP.md). | v2 spec "16 voices; oldest-steal" is canon. | 2026-09-13 |
| D-4 | Linear pan (centre = unity) for stereo halves, sqrt velocity curve, no SF2 attenuation generator. | Spec: velocity → level only; samples auditioned raw. | 2026-08-30 |
| D-5 | Loop seam: linear interpolation with the second tap wrapped into the loop; no crossfade. | Spec: crossfade only if raw looping clicks audibly — decide after the operator's ear pass. | 2026-08-30 |
| D-6 | `third_party/tsf/tsf.h` is NOT byte-verbatim upstream: 3 lines marked `/* SF2SCOUT PATCH */` add `tsf_region::sample_id` (the `sampleID` generator index). | TSF discards the hydra after load and joining region→sample by offset misattributes zones that use `startAddrsOffset` (architect finding 6). Re-apply when bumping TSF; harness `[sample-id]` pins it. | 2026-08-30 |
