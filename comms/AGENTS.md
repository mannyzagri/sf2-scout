# AGENTS — identity registry for sf2-scout

Protocol is canon at `C:\code-bank\conventions\COMMS-PROTOCOL.md` — layout,
markers and rules live there, not here. This file only declares WHICH roles
this project uses and where their inboxes are. Role definitions themselves
are `C:\code-bank\CLAUDE-WORKFLOW.md` §0 (SSOT) — never redefined here.

`comms/` is for LLM-to-LLM traffic only. Doctrine never lives here.

| Role | Platform | Lane in this project | Inbox |
|---|---|---|---|
| **vm-claude** | Windows 11 VM | Everything in the repo: engine, processor, plain-JUCE face wiring, build, validator, git. Owns `Source\`, `tests\`, `validator.json`. | `comms/to-vm.md` |
| **gui-claude** | Claude Design (web) | The face design (handoff v1 received 2026-08-29). Delivers a FULL handoff which vm-claude recreates in JUCE verbatim and wires. | **`C:\vagrant\mailbox\sf2-scout\to-gui\`** — no repo access; `comms/to-gui.md` is the repo-side INDEX of mailbox files, never the delivery channel |
| **windows-claude** (winhost) | Windows 11 host | Cubase 15 / SoundFont ear tests with the operator's SF2 library. | `C:\vagrant\mailbox\sf2-scout\to-vm\` (share mailbox; no repo access) |

**Not used by this project (yet):** mac-claude — no macOS build requested.
`comms/to-mac.md` exists as an empty inbox so the lane can open without a
scaffold change.

## Standing notes for whoever reads an inbox here

- The GUI is **plain JUCE, fixed size** (CLAUDE.md §0, D-1). The §4.0 role
  boundary still applies: the handoff README is the design; numbers it gives
  are copied, gaps are questions to gui-claude, never local inventions.
- **v2 (2026-09-12, docs/SCOUT_v2_SPEC.md): export of any sample as a
  loop-tagged WAV IS a feature.** What stays absolute: SF2 and module files are
  read-only containers, never written. Anything beyond that is a question to
  the operator, not a request to vm-claude.
