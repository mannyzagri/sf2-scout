# SF2 Scout

A VST3 / standalone **vintage sample player, loop editor and converter**: open
a SoundFont, a tracker module (.mod .xm .it .s3m and every libopenmpt format)
or a WAV, play its samples from MIDI with their own loop points and root keys,
see exactly which zone/sample fired, set and audition loop markers, and export
any sample from any source as a loop-tagged WAV (`smpl` + `bext`). SF2 and
module files are read-only containers; only WAVs are ever written.

It is a reference bench for recreating sounds by ear on hardware and for
crafting ROM loops — see `docs/SCOUT_v2_SPEC.md`.

- Behaviour spec: `docs/SCOUT_v2_SPEC.md` (supersedes `docs/DSP.md`, kept for SF2 detail)
- Face spec: `docs/handoff-gui-v1/README.md`
- Session entry for Claude: `CLAUDE.md` → `PROJECT-NOTES.md` STATE → `breakpoint.md`
- Build: `CLAUDE.md` §3 · Engine harness: `cmake --build build --config Release --target test_engine`
  (`build\Release\test_engine.exe --probe <module> [outDir]` lists and exports a module's samples)

SF2 parsing by [TinySoundFont](https://github.com/schellingb/TinySoundFont)
(MIT, `third_party/tsf/`). Tracker parsing by the
[libopenmpt](https://lib.openmpt.org/) 0.8.9 soundlib (BSD-3,
`third_party/libopenmpt/`). Playback is our own read-pointer player.
