# SF2 Scout — parameter contract

The machine-checked contract behind `Source/PluginProcessor.h` `ParamId`
(canon for IDs/encodings per `SSOT.md`). `handoff-lint.ps1` reads the fenced
block below (`validator.json` → `handoff.spec`). Append-only; never rename.

Encoding at the decoder: `continuous` = float normalised 0..1; `stepped` =
int choice index.

```json sf2scout-params
{
  "params": [
    { "id": "masterGain",  "kind": "continuous", "defaultNorm": 0.72, "name": "Master" },
    { "id": "mode",        "kind": "stepped", "steps": 2,  "defaultStep": 0, "choices": ["AS-AUTHORED", "LOOP-ONLY"] },
    { "id": "midiChannel", "kind": "stepped", "steps": 17, "defaultStep": 0, "choices": ["OMNI", "CH 1..16"] }
  ]
}
```
