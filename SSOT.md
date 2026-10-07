# SSOT manifest — sf2-scout

Names the ONE authoritative file per fact domain. Everything else stating the
same fact is a subordinate copy; a copy that disagrees with its canon is wrong
by definition. `doc-hygiene` reads this to decide direction, and refuses to run
without it. Standing rules and the amendment procedure: `C:\code-bank\conventions\SSOT.md`
(canonical — do not restate them here).

A canonical file is never archived or de-bloated by an automated pass. When the
canon itself looks wrong, the pass stops and reports upward.

**Initial draft (user ruling 2026-08-22):** vm-claude drafted this file at
project kickoff on 2026-08-30, seeded from the auditioner spec handed with the
project (`SF2_AUDITIONER_SPEC.md`, imported verbatim as `docs/DSP.md`) and the
gui-claude handoff (`docs/handoff-gui-v1/`). **The first entry is UNSIGNED —
signature requested in the kickoff batch (PROJECT-NOTES.md OPEN QUESTIONS).**

## Canon table

Shared canon is inherited from code-bank and is NOT repeated here. List only
what this project owns.

| Fact domain | Canonical file | Subordinates may |
|---|---|---|
| Project scope, era declaration, GUI type, naming contract, exemptions/deviations | `CLAUDE.md` | point only |
| Behaviour: sources, playback, editor, export, acceptance tests, build order | `docs/SCOUT_v2_SPEC.md` (received verbatim 2026-09-12; supersedes `docs/DSP.md`, `docs/SF2SCOUT_WAV_EXTENSION.md`, `docs/LOOP_BENCH_SPEC.md`) — **PROPOSED, unsigned: see amendment log** | point only |
| SF2-side detail v2 does not restate (play modes, what is ignored from the SF2 spec) | `docs/DSP.md` (the kickoff spec, imported verbatim) | point only |
| Appearance: layout, tokens, typography, interactions, state model of the face | `docs/handoff-gui-v2/README.md` (+ `SF2 Scout v2.dc.html` for interactions; received verbatim 2026-09-13; supersedes `docs/handoff-gui-v1/`) — **PROPOSED, unsigned: see amendment log** | point only |
| Machine-checked face contract (canvas, controls, actions) | `docs/handoff-gui-v2/handoff-manifest.json` (native, gui-claude; shape re-emit pending) — **PROPOSED, unsigned** | point only |
| Parameter IDs and encodings | `Source/PluginProcessor.h` (`ParamId`) | restate freely |
| Engine invariants (loop semantics, pitch math, release, bank handoff) | `tests/test_engine.cpp` (executable) | point only |
| Validator matrix + thresholds | `validator.json` (repo ROOT — `code-bank/validator/configs/` holds dated exports only) | restate freely |
| Roles this project uses | `comms/AGENTS.md` | point only |
| Current state | `PROJECT-NOTES.md` STATE | point only |
| History | `CHANGELOG.md` | — |

## Amendment log

Any role may propose; only the human enacts. Unsigned change = defect.

    Ratified-by: Menashe Zagri
    Date: 2026-08-30
    Change: manifest created for sf2-scout (drafted by vm-claude at kickoff).

    Proposed-by: vm-claude
    Date: 2026-09-12
    Change: behaviour canon moves from docs/DSP.md to docs/SCOUT_v2_SPEC.md (the
            operator's v2 directive, received verbatim from the share); export
            becomes a feature, containers stay read-only. DSP.md remains canon
            only for SF2-side detail v2 does not restate.
    Ratified-by: ____________   (UNSIGNED — operator to sign)

    Proposed-by: vm-claude
    Date: 2026-09-13
    Change: appearance canon moves from docs/handoff-gui-v1/ to
            docs/handoff-gui-v2/ (gui-claude's "Plugin GUI.zip", received
            verbatim 2026-09-13, built as 0.6.0); the machine-checked face
            contract becomes its NATIVE handoff-manifest.json (a re-emit in the
            lint's shape is pending). Engine invariants stay in tests/test_engine.cpp
            (now the three-slot decoded-sample model).
    Ratified-by: ____________   (UNSIGNED — operator to sign)
