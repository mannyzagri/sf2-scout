# SF2 Scout

A deliberately minimal VST3 / standalone SoundFont **auditioner**: load an .sf2,
play its samples from MIDI, and see exactly which zone/sample fired, its root
key vs the played note, and its loop points — with a clickable 128-key zone map
of the multisample layout.

It is a listening reference for recreating sounds by ear on hardware. It has
**no export, no record, no save**, on purpose — see `docs/DSP.md`.

- Behaviour spec: `docs/DSP.md` · Face spec: `docs/handoff-gui-v1/README.md`
- Session entry for Claude: `CLAUDE.md` → `PROJECT-NOTES.md` STATE → `breakpoint.md`
- Build: `CLAUDE.md` §3 · Engine harness: `scratch\build_test.cmd`

SF2 parsing by [TinySoundFont](https://github.com/schellingb/TinySoundFont)
(MIT, vendored in `third_party/tsf/`). Playback is our own read-pointer player.
