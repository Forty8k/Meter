# meterDARK + meterDARK Analyser: handoff (19 Oct 2026, stages 1-10 BUILT, waiting for his hand check)

## STATE AFTER THE BUILD (read this first)
All ten stages are built on the `analyser` branch (app) and `meterdark` (plugin, preset/project version 11). Nothing merged into main.
CI is the only compile/test run so far; nothing has been run on his Windows machine or his real library. Releases are SWITCHED OFF
(`release` job is `if: false` in .github/workflows/analyser.yml): download builds from the Actions tab (artifact meterDARK-Analyser-Windows).

What exists now:
- Plugin: grade `*` only when the grade is changed (Detail tune in the scoring, or a level mode); hide-hints tick (tune17); "hintLAB" marker;
  weight = thin <-> full (level-weighted bass position, 300 Hz thin / 60 Hz full); dark <-> bright is ONE gauge (balance tune18, distances tune4/tune5)
  with a width shade; frequency push retired (tune10 unused).
- Analyser: ranked grade filters (Stage 2); hintLAB (hints only; same maths as the plugin in LabHints.cpp) with "Measure low end" + scatter plots
  of old/new weight against the real low end (4a, needs his library); gradeLAB tab (the original-scoring experiment; "Use replay blocks" regrades
  from lean records and from shared records); new Library tab = overview (selected track, big tone bar, overlay lines, weight/peaks/slew bars,
  details, play/seek, three-column list, Compare versions, audio source box, Share packs...) with the old table behind "Full table".
- Lean records: `lean/<id>.lnr.gz` in the library (src/LeanRecord.*, src/ReplayPack.h). Build with the button, or CLI `--lean` / `--lean-check`.
  Replay block precision: 16 bits (zero differences; 14 also clean on synthetic music, 12 and below not). About 30 KB per minute compressed
  for the replay block (about 120 KB for 4 min), about 9 KB for the display part. Window data per track is not removed from disk.
- Share packs: `.mdarkpack` (CLI `--export-pack file [--full]`, `--import-pack file`; app: Share packs...). Imports go to `lean-shared/`.
  Records are keyed by the audio's content hash; they contain no path or file name. CI checks that.
- Audio source: editable "Source" column in the full table, box in the overview; stored in user.csv ("source") and in the lean record.
- Generator change: `OriginalResult` now also carries storeR/G/B and outputVol (the top bar colour of the latest window); regenerated `src/gen`.

WHAT HE MUST CHECK BY HAND (not testable in the cloud):
1. Plugin in a DAW: no `*` at factory settings at any session rate; `*` appears with Detail tune "grade" ticked or a level mode on; hide-hints tick;
   "hintLAB" marker position; thin/full and the one dark/bright gauge + width shade look right; presets/projects from version <=10 load without surprises.
2. hintLAB on the real library: "Measure low end" (first run takes minutes), read the two correlation numbers r (new weight should beat old);
   "Level test"; then "Export to meterDARK" and watch the plugin follow within a second.
3. Library overview on the real library: press "Build / update records" (then `--lean-check` from the CLI proves every replay block regrades
   to the same grade; it exits with 3 if not), look at the big tone bar vs the plugin's top bar for a track you know, the weight/peaks/slew bars,
   play/seek, sorting by grade, Compare versions on a song you have twice, setting Source.
4. Sizes: the real per-track and whole-library size of lean/ (the CLI prints the average KB and total MB). Decide about dropping fine blocks / pitch.
5. Share packs: export a display and a full pack, import into a second library, check shared rows show "(shared)" and gradeLAB "Use replay blocks" includes them.
6. Saved filters (filters.json) migrate and still match; ranked grade filters give the expected tracks.
Known limits: per-meter tone bars in the records are the analyser's port of scoreColumn (check by eye against the plugin); shared records
have no audio to play; "best version" = best grade, then higher sample rate; packs only carry the newest Meter version's block.

Owner: Forty8k (Windows, works at 48 kHz, listens via Tidal at -14 LUFS). Library ~2,900 tracks, mostly 96 kHz 32-bit WAV
(E:\Media\Reference Mixes / D:\AUDIO\REF Mixes).

## His rules (always)
- Explain things plainly, keep his own label words.
- **No builds without his go.** Pushing counts: `cmake_ctest.yml` runs on EVERY push to any branch, `analyser.yml` on pushes touching
  `analyser/**`. Notes-only commits: put `[skip ci]` in the message.
- **Never merge into `main`.**
- **Never hand-edit generated files** (`source/chris/`, `analyser/src/gen/`). Chris Johnson's code is never edited.
- Every new setting: 0 / off = stock behaviour, except the 96 kHz rate which is on by default (his choice).
- Work in stages, one at a time, and report back after each before starting the next.

## Repo (GitHub Forty8k/Meter, fork of Airwindows Meter)
- `meterdark` = the meterDARK plugin. CI "Pamplejuce": Windows + Linux must pass (macOS always fails on signing, ignore).
- `analyser` = the Analyser app in `analyser/`. CI "meterDARK Analyser": build, selftest, level test, replay test,
  command-line end-to-end, snapshots, auto release `analyser-b<run>`.
- Generators wrap Chris's code: plugin `tools/make_chris_layer.py` -> `source/chris/`;
  analyser `analyser/tools/make_original_scorer.py` -> `analyser/src/gen/`. Upstream sources: `chris/<ver>/`, `analyser/original/<ver>/`.
  New Airwindows versions drop in this way: the plugin stays a skin on top.
- `gh` works with REST only (`gh api ...`). `git push` prints a "repository moved" warning (harmless).

## Words used here
- **Chris's directions** = his "allow fullness / allow detail / allow activity" line. meterDARK does not show it.
- **Hints** = meterDARK's own readings that replace Chris's directions (today: light/heavy, headroom/squashed, dark/bright).
  They are the owner's attempt to make the directions useful. They never touch the grade (the one link: Detail decay / fill gate also
  go into the grade when the Detail "grade" tick is on - see stage 1).
- **Window data / core** = the 18 values Chris's audio loop sends per window (~23.4 windows per second at any rate).

## meterDARK (plugin) - what it is now
- Skin over Chris's Meter 0.5.6 (0.5.5 also kept): dark mode, per-meter tone bars with coloured playheads, legend on the main tone bar,
  display-only peak and slew trims (0 to -24 dB, never touch scoring).
- Scoring always runs at the default 1058x698 meter area, off screen (grades no longer depend on window size).
- Anchor rate: Chris's loop gets a scratch copy converted to 96k (tune[13], default on). Host audio never touched. `source/Resampler.h`
  (identical copy in `analyser/src/Resampler.h`).
- Level modes (tune[14]): original / normalised (-14 in, score as if -11) / mix (floating gain, -1 dBFS ceiling, 8 s hold, 20 s release, max +24 dB).
  Display always shows the real level.
- 17 tune settings (`kTuneCount`), saved by index as `tune0..tune16`. Presets/projects version 9. `defaults.xml` = default preset for NEW instances.
- `lab-tuning.xml` in %APPDATA%\meterDARK = the hint constants, read live (about once a second). Only affects hints.
- The hint maths lives TWICE: plugin `AirwindowsUI.cpp` (scoreColumn) and analyser `LabHints.cpp` (simulateHints). Every hint change
  must be made in both, identically.

## Analyser (app) - what it is now
- Add tracks: scans with every Meter version's own loop at the file's rate plus 44.1k and 96k (capture 1.2). "Rescan library..." upgrades old tracks.
- Library: always shows 96 kHz readings. Columns, own columns, CSV import, duplicates, grade sort (Chris's order, full tie-break),
  Filters dialog (saved sets, default exclusions, one copy per song, stored in filters.json).
- Lab: hints simulation, Level test, "Set triggers from the library", Re-score library (tunable copy of Chris's scoring: `ScoreParams` in
  `OriginalScorer.h`), "Export to meterDARK".
- Per track on disk now (4-min 96k track, uncompressed): fine blocks 6.95 MB (only used to re-estimate BPM), window data as CSV for two
  versions 1.94 MB, window data at 96k + 44.1k 0.82 MB, pitch 0.15 MB (unused after the scan) = ~9.8 MB, roughly 8-9 MB gzipped,
  ~25 GB for his library (estimate, not measured).

# THE PLAN (agreed 19 Oct). Build in this order, one stage at a time, report after each.

## Stage 1 - plugin: the grade `*`
The `*` means ONLY "this grade differs from Chris's stock grade". Show it when:
- Detail "grade" tick (tune[12]) is on AND Detail decay (tune[6]) or fill gate (tune[7]) is above 0 (this is today's `gradeTuned`), or
- a level mode is on (tune[14]: normalised or mix).
Never for: the anchor-rate conversion, hint settings, lab-tuning.xml. With factory settings: no `*` at any session rate.
Code: `AirwindowsUI.cpp` line ~614 (drop `gradeConverted` from the condition) and the comments around `gradeConverted` in
`PluginEditor.cpp` (~205) and `AirwindowsUI.h`. `gradeConverted` can be removed if nothing else uses it.

## Stage 2 - Analyser: grade filters as ranked fields
A grade like `30-Abbd` = bonus (1-80, lower better, often absent), letter (A best ... Z), then authority / power / detail.
Each sub-grade is ONE ordered scale: lowercase s (little) ... a (plenty), then capitals A ... J (K for detail) = exceptionally strong.
Filters get five fields: Bonus (number range, "none" ranks last), Letter (range), Authority / Power / Detail (each: at least / at most /
between / is capital). E.g. "Detail at least b", "Power capital", "Bonus 1-40". Not text matching. Sorting already uses the right order.
Saved filter sets in filters.json must keep loading (add a version field if the format changes).

## Stage 3 - hints switch, hintLAB name, export fix
- Plugin: new setting **"hide hints"** (0 = hints shown, so the 0-is-stock rule holds): hides meterDARK's hints from screen entirely.
  Add it as a NEW index at the end (tune17), never reuse an index. Bump presets/projects and defaults.xml to version 10.
- Analyser: rename the Lab tab to **hintLAB** (gradeLAB comes later, stage 8). Keep the file name `lab-tuning.xml` (compatibility).
- Export to meterDARK - why it seemed to do nothing (from reading the code, not tested):
  1. the constants only change hints, never grades or tone colours;
  2. exported Detail decay / fill gate only reach the grade if the plugin's "grade" tick is on;
  3. trigger values go into `defaults.xml`, which only NEW instances read (open projects keep their own);
  4. exporting untouched values = factory values = no change;
  5. a fresh `defaults.xml` is written as `ver 7`, which makes the plugin reset level mode on load. Write the plugin's current version.
  Fix: export writes the current version; the confirmation lists each value that differs from factory (or says "these are all factory
  values"), and says plainly that triggers apply to new instances and open projects keep their own.

## Stage 4 - hint rework (plugin + analyser, identical maths in both)
Do 4a first and show him the result before 4b/4c.
- **4a. Weight test in hintLAB.** For every track: the current weight reading vs the track's real share of energy below ~125 Hz
  (from the fine blocks' octave bands, F_BAND0.. in `Capture.h`: sum of the 31/62/125 Hz bands over the audible part, divided by all bands).
  Also the new "thin/full" reading below. Show both as scatter plots + correlation. Fine blocks must be kept for this.
- **4b. Weight becomes thin <-> full** (labels "thin" / "full", replacing light / heavy). Finding: today's dot mass is
  sqrt(zero-cross length x level) + (peak/140)^4 after level matching, and the peak^4 part dominates (a window at the reference gives
  ~2.2 on a 2.0-5.4 scale), so it mostly measures density - which dynamics already covers. (From reading the code: 4a checks it.)
  New reading: the average of Chris's bass position (from zero crosses) over the audible windows, each window counted by its peak
  relative to the track's own ceiling (so it does not depend on how loud the track is), mapped to 0-1.
  Remove: lowPushHz, lowPushFullHz, highPushHz, highPushFullHz, lowPushGain, highPushGain, peakInfluence, peakDiv, peakPow, and tune[10]
  (frequency push, index retired - leave it unused). Keep: balance point tune[9], thin distance tune[0], full distance tune[1], hint speed tune[8].
  CAREFUL: the weight block also computes massCeil, the level reference, detailF (used by Detail's level match and the quiet gates) and the
  dynamics spread. Those must stay working.
- **4c. Dark <-> bright becomes ONE gauge** like the other two. Each window is green (peaks ahead of slews = dark), red (slews ahead =
  bright) or neutral (close together = balanced). Reading: tilt = weighted red share - weighted green share (same run-length / lone weighting
  as now), -1 dark ... +1 bright, balance point in the middle. PLUS a **width** shade behind the needle = red share + green share (how much of
  the time is not neutral), so a balanced track and one flipping between dark and bright look different.
  Settings: balance point (NEW index tune18), dark distance (tune4), bright distance (tune5). Remove brightSpread. Keep detail gate, run
  length, lone weight, detail decay, fill gate.
- Version bump to 11: older presets reset the changed settings (tune0, tune1, tune4, tune5, tune9) to the new defaults, as earlier bumps did.
  "Set triggers from the library" must handle the new gauges. hintLAB's level test must still pass.

## Stage 5 - measure the replay block (no format change yet)
Goal: one small block per track that rebuilds grades, tone bars and hints exactly, with any Meter version that reads the same values.
Chris's 0.5.6 scoring uses ALL 18 window values (peaks, slews, zero crosses -> scores; beam/departure lines -> the black line in the bonus).
His bins fill and drain every window, so totals are not enough: the window data itself is the minimum.
Replay block = the 96k window data (18 values x ~23.4 windows/s), each value as a log-scaled integer, delta-coded, compressed.
16-bit = 202 KB per 4 min before compression (estimate 120-200 KB after).
Test (a CLI command + CI-style check): replay his whole library from blocks at 16, 12 and 10 bits vs the exact data; count tracks whose
grade, bonus or tone colours differ. Report sizes and differences. Use the smallest precision with ZERO grade differences. Report to him
before stage 6.
Limit to state honestly: if Chris adds a NEW reading in a future version, the audio is needed again.

## Stage 6 - the lean record (local format + converter)
Per track:
- **Display record (~7 KB compressed, 1024 columns per track; tracks under ~44 s get one column per window):**
  overall tone bar (Chris's tone colour per window, averaged into columns, RGB 1 byte each), weight / peaks / slew tone bars (meterDARK's
  per-meter tone from scoreColumn: mirror that maths in the analyser like the hints, 1 byte each), overlay lines peak / weight / detail (Chris's peak, bass position, slew per window, 1 byte each),
  grade + sort values + Meter version, peak, true peak, LUFS, sample rate, bit depth, length, audio source, artist / title / album / year.
- **Replay block** (stage 5 size).
- Kept local only, never shared: file paths, own columns / notes, fine blocks, pitch, other rates' window data.
Converter: build records for existing tracks from their stored 96k window data (no audio rescan). Tracks not yet rescanned to 96k need
"Rescan library" first. The replay block can then replace the 96k CSV / float files on disk. Dropping fine blocks and pitch is HIS decision
later (after stage 4a); do not delete them.

## Stage 7 - new Library tab (his sketch)
- Top ~40%: selected track. Name; large overall tone bar with the grade, overlay lines on it; separate weight, peaks, slew tone bars;
  technical details (peak levels, sample rate, audio source); play / stop with a playhead and click-to-seek when the file is on this machine.
- Bottom ~60%: track list with only three columns: track name, small overall tone strip (drawn from the big bar's data), sortable grade.
  Stacked strips should show trends across the library. Search, filters, duplicates stay above the list.
- Full detail stays reachable: the current table behind a toggle, plus the double-click details window.

## Stage 8 - gradeLAB
Its own tab beside hintLAB: sliders for `ScoreParams` (decays, bin cap, curves, grade/letter curves, clip handling, black line, tone decay),
before/after grades across the library, replayed from the replay blocks (so it works on shared tracks too). Grades from gradeLAB settings
in the plugin, if ever exported, DO count for the `*`.

## Stage 9 - audio source + versions
- Audio source field (Tidal, Spotify, Apple Music, vinyl, CD, HDtracks, Qobuz, other / his own words): set per track, on a filtered
  selection, or by CSV import. Stored with his own values (user.csv), shared in packs.
- Duplicates grouped by song; mark the best version; compare two versions with tone bars stacked on the same time scale
  (e.g. vinyl original vs modern remaster).

## Stage 10 - share packs
- Display pack (records only, ~7 KB per track) and full pack (records + replay blocks). Import merges others' scans; each record keeps
  its Meter version. Shared: never paths or private columns.

## Still open
- Chris's own reference rate and loudness are unknown (96k and -11 LUFS are assumptions).
- The 25 GB library size is an estimate; stage 5 should report the real figure.
