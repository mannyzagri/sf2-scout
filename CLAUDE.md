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

# SF2 Scout — SoundFont audition / reference tool (VST3 + Standalone)

**Read `docs/DSP.md` "Purpose" first — it scopes everything down.** SF2 Scout
loads an .sf2, plays its samples from MIDI, and shows exactly which zone/sample
fired, how far it is pitch-stretched, and where its loop points are. It is a
listening reference for a by-ear recreation workflow on hardware. It is NOT a
SoundFont synthesizer, NOT a converter, and must never grow export/record/save.
Days-scale project; resist scope growth.

## §0 Kickoff declarations (2026-08-30, vm-claude — operator to confirm; see PROJECT-NOTES OPEN QUESTIONS)

| Declaration | Value | Why |
|---|---|---|
| Era-math regime | **modern / n.a.** — the tool is a sample read-pointer with linear interpolation; no synthesis layer exists to be era-faithful about. `vintage-dsp` does NOT apply. | docs/DSP.md "What to IGNORE" |
| GUI type | **fixed** — one 920-px-wide plain-JUCE face, not resizable. | handoff README "No responsive behaviour needed — fixed-size plugin window" |
| GUI technology | **plain JUCE Components — NOT the house WebView pattern.** Recorded deviation D-1. | docs/DSP.md "Tech approach": *"No editor framework beyond stock JUCE components … WebView GUI NOT required"*; handoff README "About the Design Files" says the same |
| JUCE acquisition | FetchContent pinned to 8.0.4, satisfied offline by `-DFETCHCONTENT_SOURCE_DIR_JUCE=C:/rhino/deps/JUCE` (rhino pattern). | no network dependency at configure |
| SF2 parsing | **TinySoundFont v0.9** (MIT), vendored verbatim at `third_party/tsf/tsf.h` + `LICENSE`. Used for parsing only; playback is our own (spec "Tech approach"). | docs/DSP.md |
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
| `docs/DSP.md` | behaviour: scope, play modes, release, what to ignore, NON-features, acceptance tests, build order. **Verbatim import of `SF2_AUDITIONER_SPEC.md` (2026-08-29).** |
| `docs/handoff-gui-v1/README.md` | appearance: every colour, size, font, spacing, interaction of the face. Verbatim from the gui-claude bundle (2026-08-29). `docs/handoff-gui-v1/handoff-manifest.json` is the machine-checked part (**derived** by vm-claude — a native one is requested for v2). |
| `PROJECT-NOTES.md` STATE | current state — deployed build, validator, params, pending |
| `breakpoint.md` | session-end snapshot, subordinate to STATE |
| `CHANGELOG.md` | history; item IDs `SC-nnn` |
| `validator.json` (repo root) | validator matrix; `code-bank/validator/configs/` gets dated exports only |
| `FEATURE-INDEX.json` | feature → file/symbol map; check before grepping |
| `comms/AGENTS.md` | which roles this project uses |

## §2 Non-negotiable rules

1. **NON-features are absolute** (docs/DSP.md): no audio export, no sample
   extraction, no save-as, no drag-out of audio, no preset editing, no writing
   to the SF2, no hosting inside The Dreamer. A request that touches these is
   surfaced to the operator, never quietly built. `.gitignore` refuses `*.sf2`.
2. **Parameter IDs are a public API.** `masterGain`, `mode`, `midiChannel` —
   append-only, never rename/reorder (`Source/PluginProcessor.h` `ParamId`).
   Encoding at the decoder: float normalised 0..1; choice = int index.
3. **Audio thread**: no allocation/locks/logging. Bank swap = atomic pointer
   handoff (`ScoutEngine::setBank` / `takeRetiredBank`); the audio thread
   never frees. UI audition notes travel through an `AbstractFifo`.
4. **The engine is JUCE-free** (`Source/engine/`) and is proven by the cl.exe
   harness `tests/test_engine.cpp` BEFORE any plugin build. It builds a
   structurally complete SF2 in memory, so no third-party SoundFont is ever
   needed or committed.
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
scratch\build_test.cmd            # engine harness (cl.exe, JUCE-free) -> ALL CHECKS PASSED
powershell -ExecutionPolicy Bypass -File C:\code-bank\validator\validate.ps1 -Project C:\sf2-scout
```
Artefacts: `build\Sf2Scout_artefacts\Release\VST3\SF2 Scout.vst3`,
`build\Sf2Scout_artefacts\Release\Standalone\SF2 Scout.exe`.
Deploy targets: `validator.json` `deploy.targets`. Compiles go through
`build-runner`.

## §4 Phase plan (docs/DSP.md "Build order") — every phase has a gate

| Phase | Scope | Gate | Status |
|---|---|---|---|
| 0 | Scaffold, SSOT draft, naming, harness, first build | harness ALL CHECKS PASSED; VST3 + Standalone build; pushed | **2026-08-30** — see STATE |
| 1 | Load + preset list + AS-AUTHORED playback | acceptance 1 (three SF2s load, list, play) — operator ear | code complete, unaudited |
| 2 | Own read-pointer path: two modes, release fade, 32-voice pool | acceptance 2, 3, 5 — harness `[authored]` `[looponly]` `[pitch]` `[release]` + operator ear | harness green, unaudited |
| 3 | Info readout + zone piano strip | acceptance 4 — chromatic scale across a zone boundary flips the readout at the boundary key | code complete, unaudited |
| 4 | Drag-drop, polish, pluginval 5, Cubase 15 | acceptance 6, 7 | pluginval 5 PASS 2026-08-30 |

## §5 Test material

None committed (rule 1). The harness fabricates its own SF2. For ear tests the
operator supplies three files on the host (GM bank, single-instrument bank,
synth bank — docs/DSP.md acceptance 1); they live on the share, never in git.

## §6 Recorded deviations

| # | Deviation | Why | Date |
|---|---|---|---|
| D-1 | Plain JUCE Components instead of the house JUCE-8-WebView face; validator `gui` stage (headless Chrome + fake bridge) does not apply and is omitted, not faked. | Spec + handoff both mandate stock JUCE; a bench tool built in a day. `juce-webview` persona is therefore situational here, not mandatory. | 2026-08-30 |
| D-2 | IBM Plex Sans/Mono not bundled; JUCE default sans + Consolas. | Handoff's own substitution rule ("keep the sans/mono split"). Bundling Plex is a one-line change if the operator wants it. | 2026-08-30 |
| D-3 | Window height is the SUM of the handoff's stated row heights (48 + 310 + 120 + 44 = 522 px) rather than the ~700 px of the Claude Design preview frame. | The README says "scale down proportionally if a smaller default window is required, do not shrink the strip below 48 px or numerals below 16 px" — both kept at full size; 700 was the preview canvas, not a row sum. | 2026-08-30 |
| D-4 | Linear pan (centre = unity) for stereo halves, sqrt velocity curve, no SF2 attenuation generator. | Spec: velocity → level only; samples auditioned raw. | 2026-08-30 |
| D-5 | Loop seam: linear interpolation with the second tap wrapped into the loop; no crossfade. | Spec: crossfade only if raw looping clicks audibly — decide after the operator's ear pass. | 2026-08-30 |
