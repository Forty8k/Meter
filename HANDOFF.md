# meterDARK + meterDARK Analyser: handoff (19 Oct 2026)

Owner: Forty8k (Windows, works at 48 kHz, listens via Tidal at -14 LUFS). Library ~2,900 tracks, mostly 96 kHz 32-bit WAV
(E:\Media\Reference Mixes / D:\AUDIO\REF Mixes). Explain things plainly, keep his own label words, no builds without his go,
never merge into `main`, never hand-edit generated files.

## LATEST: 23 Oct 2026, Meter 0.5.8 (Opus)
- chris/0.5.8-11a8ac8 added, generator run: Current.h on 0.5.8, kChrisVersion 508, VERSION 0.5.8. Chris's only change: the bonus number shows up to 99 (was 80)
  and only with an A balance. That is in his scoring code (untouched); meterDARK shows whatever his code gives. The 0.5.7 slew-line yellow (chrisVersion >= 507) stays.

## LATEST: 22 Oct 2026, Meter 0.5.7 (Opus)
- chris/0.5.7-e0faab5 added, generator run: Current.h on 0.5.7 with `constexpr int kChrisVersion = 507`. VERSION 0.5.7.
- 0.5.7 thins Chris's slew (detail) black line when his beam is above 186 and drops it to nothing above 198 (that part of the score counts less). meterDARK's
  own drawing (AirwindowsUI.cpp, `brightCut`) keeps the line at its full 0.5.6 body and blends it to the clip yellow by how much his rule cuts it (fully
  yellow above 198), using his unscaled beam so the zoom does not move it. Scoring is his code, untouched. Light mode draws as before.
- 0.5.7 directions only show without a bonus number; meterDARK does not draw directions, nothing to do.
- Not checked on a real screen yet.

## Repo (GitHub Forty8k/Meter, fork of Airwindows Meter)
- `meterdark` = the meterDARK plugin. CI "Pamplejuce": Windows + Linux must pass (macOS always fails on signing, ignore).
- `analyser` = the Analyser app in `analyser/`. CI "meterDARK Analyser": build, selftest, level test, replay test,
  command-line end-to-end, snapshots, auto release `analyser-b<run>`.
- Chris Johnson's code is never edited. Generators wrap it: plugin `tools/make_chris_layer.py` -> `source/chris/`;
  analyser `analyser/tools/make_original_scorer.py` -> `analyser/src/gen/`. Upstream sources: `chris/<ver>/`, `analyser/original/<ver>/`.
  New Airwindows versions drop in this way: the plugin stays a skin on top.
- `gh` works with REST only (`gh api ...`). `git push` prints a "repository moved" warning (harmless).
- Every new setting: 0 / off = stock Chris, except the 96 kHz rate which is on by default (his choice).

## meterDARK (plugin) - what it is now
- Skin over Chris's Meter 0.5.8 (0.5.5, 0.5.6 and 0.5.7 also kept; `python3 tools/make_chris_layer.py <id>` picks another): dark mode with correct colours (including 0.5.6's new lines),
  per-meter tone bars with coloured playheads, legend on the main tone bar, display-only peak and slew trims (0 to -24 dB, never touch scoring).
- Fixed: grades depended on window size -> scoring always runs at the default 1058x698 meter area, off screen.
- Fixed: identical audio at different sample rates graded differently (Chris's slew = sample step x rate, peak = highest sample).
  Chris's loop now gets a scratch copy of the audio converted to an anchor rate: tune "rate" off / 44.1k / 96k, default 96k.
  Host audio is never touched. Resampler: `source/Resampler.h` (identical copy in `analyser/src/Resampler.h`).
- Level modes in tune: original (default) / normalised (input at -14, score as if at -11 LUFS) / mix (floating gain, -1 dBFS ceiling,
  8 s hold, 20 s release, max +24 dB). Display always shows the real level (gain removed again).
- Grade shows a single `*` when it is not exactly Chris's stock grade (Detail tune ticked, a level mode on, or audio being converted).
- Presets/projects version 9 (older ones load with 96k on).
- HINTS (dark / bright / squashed / headroom / light / heavy): made level-independent (gates relative to the track's own ceiling),
  but the readings still aren't right. He's leaning towards removing them, reclassifying what each meter can really show, and rebuilding simpler.

## Analyser (app) - what it is now
- Add tracks: scans audio with every Meter version's own loop at the file's rate plus 44.1k and 96k (capture 1.2).
  "Rescan library..." upgrades old tracks (resumable). His rescan was in progress; 48k tracks need it before their 96k readings exist.
- Library: always shows 96 kHz readings (no rate or level pickers; a track not rescanned yet shows its own readings).
  Columns, own columns, CSV import, duplicates, grade sort (Chris's order with full tie-break: bonus, letter value, hidden bonus,
  authority+power+detail, name). Filters dialog: conditions, saved named sets, "Lab uses the filtered tracks",
  default exclusions (<60 s, >10 min, < -25 LUFS, no grade), one copy per song. Stored in filters.json.
- Lab: meterDARK hints simulation (mirror of the plugin maths in `LabHints.cpp`), Level test, "Set triggers from the library",
  original-scoring experiment (Re-score library), "Export to meterDARK".
- Engine still has level-mode grading (`Library::levelGrades`, CLI `--levels`), unused by the screens.

## Known problems / open
1. Grade filtering is too crude. A grade like `30-Abbd` is: bonus number (1-80, lower better, often absent), overall letter
   (A best ... Z), then authority / power / detail. Each sub-grade is ONE ordered scale: lowercase s (little) ... a (plenty),
   then capitals A ... J/K (exceptionally strong). Filters should treat each part as its own ranked field (e.g. "Detail at least b",
   "Power capital", "bonus 1-40"), not as text.
2. "Export to meterDARK" seems to do nothing visible. It writes `%APPDATA%\meterDARK\lab-tuning.xml` (constants, read live)
   and `defaults.xml` (trigger values, ver 7). Paths match the plugin. Suspects, not yet checked: default preset only applies to NEW
   instances (an open project keeps its own tune values); the exported settings may be ones with no visible effect; version handling.
3. Hints need a rethink (see above).
4. Chris's own reference rate and loudness are unknown (96k and -11 LUFS are assumptions).
5. He has a new idea for the Analyser library (to be discussed in the new chat).
