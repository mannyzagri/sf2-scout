# SF2 Scout v2 — GUI handoff v2

From: gui-claude · 2026-09-13 · project sf2-scout · for vm-claude
Bundle: `README.md` (this) · `handoff-manifest.json` · `SF2 Scout v2.dc.html` + `support.js` (prototype; open the .dc.html in a browser from the same folder).

## CHANGES FROM V1

1. Window **920 × 796 → 1200 × 840**, still fixed layout. Whole face is a 1200 × 840 canvas; overall size is a single uniform **scale factor 0.75–1.5** (prototype: Scale tweak). Plain JUCE: `setResizable(true,true)`, `setFixedAspectRatio(1200/840)`, `root.setTransform(AffineTransform::scale(k))`. WebView2 is an acceptable alternative if the operator wants it later; not required.
2. **IBM Plex Sans + Plex Mono bundled** (retires deviation D-2). Sans for labels/buttons/names, Mono for every numeral, id, path, chip.
3. Preset column (300) → **SOURCE LIST (204) + CONTENTS (296)**. Source list is a tree: one row per open source with a **type badge** (SF2 blue / IT·XM·MOD·S3M green / WAV orange / failed load red); SF2 rows expand to presets. Contents shows the samples of the selected preset / module / WAV. This absorbs the v1 preset list, → W and the Slot W title chooser (dropped).
4. Readout is **always two columns A | B** (A left). Empty slot shows a hint. Zone map follows whichever slot holds an SF2 zone (A first).
5. KEYBOARD PLAYS **SF2/WAV/SPLIT → A / B / SPLIT / TOGGLE** + split-note field. Slot W concept retired: the editor edits whatever sample is selected, from any source.
6. New **EXPORT column** (255 px, right, full height between header and footer): folder + quick targets, audio/stereo options, EXPORT SAMPLE, EXPORT RANGE, BATCH with scope, skip-existing, progress, last-files log; **METADATA box** bottom-anchored (SF2 INFO / MODULE INFO / WAV CHUNKS).
7. Editor gains **SUGGEST, AUTO-DETECT ROOT, PERIODS (warn tint), CLICK METER, LOUDNESS**; waveform 108 → 140 px; UNLOAD/LOAD WAV/16-BIT MONO moved out (LOAD is global, 16-bit is an export option).
8. Footer: DEVICE chip is now a labelled ComboBox; QWERTY/SPACE hint added; voices 16.

## Design tokens (unchanged from v1 = Palette.h)

window #f6f6f3 · headFoot #efefea · zoneBand #f1f1ec · inset #ffffff · borderOut #c9c9c2 · borderSec #d7d7d0 · hairline #e3e3dc · hairlineRow #edede6 · control #d0d0c8 · thumb #b6b6ae · zoneEdge #7d8a97 · text #1c1c1a · text2 #5c5c54 · label #8a8a80 · text3 #a3a399 · accent #2e5c8a · accentHov #24486d · selRow #dfe8f1 · loopFill #c7d9ea (55 % on waveform) · zoneA #cfdcea · zoneB #e7edf4 · warn #d9772b · okDot #4c9a5e · idleDot #c2c2ba · sliderTrack #dcdcd4 · switchTrack #e2e2db · barBase #e6e6df · hoverBtn #eef2f7 · mStart #4c9a5e · mEnd #c2453a · wave #2e5c8a · playhead #d9772b.
New: **badgeSf2 = accent**, **badgeMod = okDot**, **badgeWav = warn**, **badgeErr = mEnd (#c2453a)**, **periodsWarnBg #fbeee2**, **clickYellow #d9b52b**, slotA tag = accent, slotB tag = text2.

### Type scale (Plex)
- smallLabel: Sans 500 10 px, letter-spacing 1 px, uppercase, colour label.
- button: Sans 700 11 px, letter-spacing 0.9 px, uppercase.
- listHeader: Sans 500 9 px, letter-spacing 0.8 px, text3.
- name: Sans 400 12 px (500 for source rows).
- mono 9 (badges, bold, letter-spacing .5) · mono 10 · mono 11 (rows, paths) · mono 12 bold (fields, chips) · mono 13 bold (header filename) · mono 20 bold (readout numerals; 11 bold suffix for cents).

### Widgets
- Accent button: fill accent, text #fff, radius 4, hover accentHov.
- Flat button: fill inset, 1 px control border, radius 4 (3 in list headers), text text (text2 in list headers), hover hoverBtn.
- Segmented: track switchTrack, 1 px control border, radius 4, 1 px inner pad, 2 px between cells; on-cell accent / #fff, radius 3; disabled cell text3; a selected-but-disabled cell paints thumb.
- Field: inset fill, 1 px control border, mono 12 bold right-aligned (text fields left, mono 12 regular), 4 px side pad; focus border accent. Enter commits, Esc reverts, blur commits.
- Read-only field: same box, border hairline.
- Checkbox: 17 × 17 (15 in export column), radius 4, control border, ✓ in accent mono 12 bold; label Sans 12 text2.
- Badge: 15 × auto, 4 px side pad, radius 2, mono 9 bold #fff on type colour.
- Chip (footer combos): 28 high, control border, radius 4, mono 12 bold, ▾ in text3.

## Layout — rows (y, h) at scale 1.0

header 0/48 · body 48/320 · zone band 368/104 · editor 472/324 · footer 796/44. Dividers 1 px borderSec at the bottom of header, body, zone band; left of export column at x 944. Side padding 16.
Columns: main 0–944 · export 945–1200 (255).

### 1 Header (0,0 1200×48, fill headFoot)
Flex row, padding 0 16, gap 14, vertically centred.
LOAD 16,9 64×30 accent · SOURCES OPEN block x 94 w 250 (label 10 px at y 12, filename mono 13 bold at y 26, ellipsised) · spacer · "KEYBOARD PLAYS" smallLabel text2 · segmented 232×32 cells A 44 / B 44 / SPLIT 62 / TOGGLE 70 · split field 48×24 (note name, e.g. C4; 45 % opacity unless SPLIT) · 10 px · "MODE" · segmented 185×30 AS-AUTHORED 95 / LOOP-ONLY 82. Right group is right-aligned from x 1184; resolved x: mode seg 999, MODE label ≈ 955, split field 883, kb seg 637, KEYBOARD PLAYS ≈ 533.
Reads: sources[], slotA, slotB, kbMode, splitNote, auditionMode. Writes: kbMode (disabled cells: A needs slotA, B needs slotB, SPLIT/TOGGLE need both), splitNote (0–127, note name or int), auditionMode.

### 2 Body (0,48 944×320)
**2.1 Source list** 0,48 204×320, border-right borderSec.
Header row 42 high: "SOURCES" smallLabel x 12; UNLOAD flat 22 high mono 10 bold, right, 10 px from edge; hairline under.
Rows 32 high, hairlineRow under, padding-left 8 (26 for presets), gap 7: caret 10 (▸/▾ SF2, ● module/WAV, ! error) mono 11 text3 · badge · name (Sans 500 12; error rows Sans 12 in badgeErr, text "file — reason") · right sub mono 10 text3 ("9 presets" / "25 smp" / "24b st" / "4 zn"). Selected source or preset: selRow. Preset rows: badge = bank:program on text3 (#a3a399).
Last row: drop hint Sans 11 text3.
Reads: sources, expanded, selSrc, selPreset. Writes: selSrc, expanded, selection → first item of that source. Clicking an error row re-posts the error to the editor status; UNLOAD removes the selected source and clears its A/B slots (confirm if a WAV has unsaved edits).
Empty: only the drop hint.

**2.2 Contents** 204,48 296×320, border-right.
Header 42: kind label ("IT SAMPLES · 25" / "ZONES OF PRESET" / "WAV SAMPLE") over title (mono 11 bold, file or "000:002  Grand Piano"); right: → A 34×22, → B 34×22, ‹ 22×22, › 22×22 (gap 6). Column header row 18 high (listHeader): # 18 · NAME flex · FMT 40 · FRAMES 36 · LP 22 · HZ 36, gap 6, padding 0 12.
Rows 26 high, hairlineRow under, hover hoverBtn, selected selRow: idx mono 11 (accent when selected, else text3) · name Sans 12 (SF2 zones: "Piano_C5  C4–B5") followed by A / B tags (mono 9 bold #fff on accent / text2, radius 2) · fmt "8b mo" mono 10 text2 · frames mono 10 · loop kind mono 10 bold coloured fwd text2 / pp accent / off text3 / sus warn · rate mono 10.
Click selects (editor + readout follow), double-click plays at root, ‹ › and , . step. Empty: "No source selected." / for a failed load: "ERROR: file: reason. Previous state kept." Sans 11 text3.

**2.3 Readout** 500,48 444×320, padding 12 16. Two columns 201 wide, gap 14, hairline between (column A has 14 px right padding + 1 px border).
Per column: row 16 — slot square 16×16 (A accent / B text2, mono 11 bold #fff), type badge, path mono 11 text2 ellipsised ("fall_in_love.it ▸ 03 Jazz Piano3"), live dot 7 px (okDot when the keyboard currently reaches this slot, else idleDot). 12 px gap. Big row 38: PLAYED (accent) · ROOT (+ cents suffix mono 11 text2) · STRETCH (warn when |st| > 7), smallLabel over mono 20 bold, gap 10. 12 px gap. Rows 17 high mono 11: key 66 wide label colour, value: source, format, length, loopStart, loopEnd, loop len (smp · ms · periods; warn when periods non-integer ±0.05), loop type (+" (override)"), zone (SF2) or loudness (others).
Empty slot: badge "—" idleDot, path "slot B empty", numerals "—", hint text Sans 11 text3.

### 3 Zone band (0,368 944×104, fill zoneBand), padding 11 16
Label row y 379 h 12: "ZONE MAP — A · Grand Piano — CLICK A KEY TO AUDITION" left, "4 ZONES · SPLIT C4" right. Strip y 396 h 52, x 16–928 (912 = 128 × 7.125), inset fill, borderOut border radius 3: cell fill zoneA/zoneB alternating per zone, #fff outside zones, e7edf4 when no zones; black keys 58 % overlay rgba(28,28,26,.30); C marks 4 px bottom rgba(28,28,26,.35); zone right edge 1 px zoneEdge; last SF2 note accent; split note warn (SPLIT mode only). Click plays that key on the zone's sample. Labels y 452 h 20: per zone, left border control, 6 px pad, mono 10 two lines (name text2, "C4–B5  root C5" text3).
No SF2 in A/B: title "ZONE MAP — NO SF2 IN A/B", count "—", strip all e7edf4, clicks ignored.

### 4 Editor (0,472 944×324), padding 10 16
Title row y 482 h 12: "EDITOR" smallLabel · type badge · "A ▸ path" mono 11 bold (● suffix when a WAV has unsaved edits) · status right, mono 11 bold, okDot for confirmations, warn for errors (one line, previous state kept).
Controls y 502 h 26, gap 8: PLAY 84 (▸ PLAY C4 flat / ■ STOP accent, latching) · "LOOP" · seg 179 FWD 44 / PING-PONG 83 / OFF 44 · ZERO-X SNAP checkbox · SUGGEST flat · AUTO-DETECT ROOT flat · spacer · SAVE AS 76 flat · SAVE 60 accent (40 % opacity + tooltip "IT is a read-only container — use EXPORT SAMPLE" unless the sample is a WAV).
Waveform y 534 h 140, x 16–928, inset, borderOut, radius 3: loop region loopFill 55 %; centre hairline; min/max column trace wave 1 px; START marker 2 px mStart with label mono 9 bold at +3,+3; END 2 px mEnd label right; playhead 2 px playhead (hidden when stopped); bottom-right "v0 .. v1 (n smp/px)" mono 10 text3; bottom-left zoom hint. Left-click/drag: nearer marker; right-drag: END; wheel: zoom about cursor (min 64 smp); shift-drag or middle-drag: pan; release with ZERO-X SNAP: marker snaps to the nearest rising zero crossing within ±64 smp.
Lower row y 682 h 80: SEAM VIEW 16,682 300×80 (title "SEAM 20 ms", "step 0.012" right mono 10 text2, END half in mEnd 90 %, START half in mStart 90 %, centre line 1 px text2 at x 149, live while dragging). Fields from x 328, gap 8, label 11 + 2 + field 22:
 row 1 (y 682): LOOP START 84 · LOOP END (INCL) 84 · LOOP LEN 84 · PERIODS 68 (read-only; warn text on periodsWarnBg when non-integer) · ROOT 52 (note name) · FINE c 52 (−99..99) · ATTACK ms 56 (0–500) · RELEASE ms 56 (10–5000).
 row 2 (y 722): CLICK METER 110 (three 10 px cells green/yellow/red lit cumulatively, ratio mono 11 bold right; <1 green, 1–2 yellow, >2 red) · LOUDNESS 84 (read-only, "-14.2 dB") · SAVE AS PREFIX 110 (text) · BEXT DESCRIPTION flex (text, auto-generated provenance, text2, editable).
Status line y 770 h 14 mono 10: cursor/loop line left 470 (text2), hotkey hint right (text3).
Empty: waveform shows "drop a source or pick a sample" bottom-right, fields blank, PERIODS/CLICK/LOUDNESS "—".

### 5 Export column (945,48 255×748), padding 10 12 8, gap 10
EXPORT header 14 ("smpl + bext" right mono 10 text3) · FOLDER: field 24 high (path mono 11, rtl-ellipsised) + … 26×24; quick targets "reference" / "rom" 20 high segmented-style flat buttons (selected = accent) · AUDIO seg NATIVE / 16b/44.1 MONO (22 high, label column 56) · STEREO seg SUM / L ONLY (45 % opacity unless source is stereo or 16-bit conversion) · note mono 10 text3 ("writes 8287 Hz 8→16-bit mono") · hairline · EXPORT SAMPLE 28 accent, target file name under it mono 10 text2 · RANGE START / END (EXCL) fields + EXPORT RANGE flat 24 + note ("15740 smp · markers remapped" text3 / "markers outside range" warn) · hairline · BATCH seg scope (MODULE|PRESET|FILE by source type) / SOURCE · skip existing files checkbox 15 · EXPORT ALL 28 accent (running: flat "CANCEL" in mEnd) · progress bar 12 high barBase/borderSec, fill accent · "writing 18 Bell Glass" left, "17 / 25" right mono 11 · last 4 files mono 10 text3 (done line okDot, cancelled line warn) · spacer · hairline · METADATA: title "SF2 INFO"/"MODULE INFO"/"WAV CHUNKS" + badge, box inset/hairline radius 4, rows mono 10 key 44 + value (multi-line message allowed), max 150 high, scroll.
Reads: cur sample, folder, fmt, fold, range, scope, skip, batch progress. Writes: all of them; EXPORT actions return one status line to the editor title row.

### 6 Footer (0,796 1200×44, fill headFoot), padding 0 16, gap 12
MASTER 52 · slider 200×20 (track 6 sliderTrack, fill accent, thumb 14×16 #fff/thumb) · dB mono 12 bold 56 · build stamp mono 9 text3 · spacer · "QWERTY piano Z–M · SPACE latch" mono 10 text3 · DEVICE + ComboBox chip (standalone only) · MIDI IN + chip 76 (OMNI, CH 1–16) · voices 104: 8 px dot okDot/idleDot + "2/16 voices" mono 12 text2.

## Keyboard
F / P / O loop mode · [ ] { } START ±1/±100 · ; ' : " END ±1/±100 · , . prev/next sample in the contents list · TAB swap A/B (forces TOGGLE) · SPACE latch-play at root / stop · L toggles AS-AUTHORED/LOOP-ONLY · Ctrl+S save · Ctrl+Shift+S save as · wheel zoom, shift-drag pan on waveform. Fields swallow keys while focused; Enter commits, Esc reverts.

## State model
- sources[]: {id,type,file, presets[]|samples[]|sample, metadata, error?}
- selection: selSrc, selPreset, cur (sample key "src:preset:zone" / "src:index" / "wav")
- slots: slotA, slotB (sample keys), kbMode A|B|SPLIT|TOGGLE, toggleOn, splitNote
- per-sample session edits: loopStart, loopEnd, root, fine, attack, release, prefix, desc, loopOverride — kept in memory for SF2/module samples, written back on SAVE for WAVs only
- audition: mode, playing, playhead, lastNote{A,B}, lastSf2Note
- editor view: v0, v1 (frames), snap
- export: folder, quick, fmt, fold, rangeStart, rangeEnd, scope, skip, batch{running,done,total,skipped,log}
- footer: gain, midiChannel, device, voices

## Answers to brief §7
1. Source list sits beside the contents column (204 + 296); together they replace the 300 px preset column.
2. Export is a permanent right-hand column; progress and folder stay visible while playing.
3. Slot W title chooser dropped; the source list/contents take over.
4. Readout is always two columns A | B; the zone strip follows the slot holding an SF2 zone (A first), the highlighted key is the last SF2 note played.

## Manifest note
`contract/MANIFEST-FORMAT.md` was not in the bundle I received, so `handoff-manifest.json` follows the shape of the derived v1 manifest as described (canvas, params, controls with id/type/label/bounds, actions, gaps). If the lint wants other field names, send the schema and I'll re-emit — no design change needed.
