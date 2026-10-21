# Next build (21 Oct 2026): fixes from the user's notes. Built by Sonnet, plan by Opus.
Read HANDOFF.md first (rules, local compile check, CI). Branch `analyser` only (no plugin change in this round). Commit + push after each step, CI green before the next.
When all steps are done: update HOW-TO.txt and the LATEST section of HANDOFF.md, push, then SendUserMessage + PushNotification "Fixes done and pushed", and stop.

## 1. One grade order everywhere (bug: four views sort grades differently)
Cause: the Full table sorts by number, hidden bonus value, then the SUM of authority+power+detail values (so 1-AABA beats 1-AAAA); the Overview (GlanceView `resort`, key = bonus
or 1000-letterValue) and gradeLAB (`ranks()` + `refresh()`, origRank 0 for tracks not in use / before load) have no tie-breaks, so the many tracks that share "1-" keep load order
(looks alphabetical); hintLAB sorts the grade TEXT.
Fix (the user's decision, 21 Oct): sort grades the way Chris does, by the grade TEXT, like a file-name column in Finder (hintLAB already behaves like this and the user
likes it). Add to GradeRank.h one pure function (no JUCE) used by all four views:
  int compareGradeText(const std::string& a, const std::string& b)   // <0 if a sorts above b (best first)
  rules: an empty grade (not graded / not scored) always goes LAST, in both sort directions;
         the bonus number is compared as a NUMBER (1- before 2- before 10- before 80-), like Finder's natural sort; a grade with a number comes before one without
         (digits sort before letters in Finder anyway);
         then the letters left to right, case-insensitive (A before B ... like Finder);
         only if the letters are equal ignoring case: capital before lowercase at the first difference (A is stronger than a on Chris's scale), so the order never depends on load order;
         the caller breaks a full tie by track name.
  Reverse sort = the same order backwards, except ungraded stays last.
Use it in: MainGui.cpp sortShown (Kind::Grade; replace the bonus/letter-value/values key), GlanceView resort (col 3), LabView hintLAB grade column, gradeLAB "Original grade" and
"Lab grade" columns (sort on the grade text, not on origRank/labRanks; rank columns keep their numbers). The gradeLAB rank numbers (`ranks()`) stay Chris's internal order.
Add cases to tools/selftest.cpp: "1-AAAA" above "1-AABA" above "1-BDDD"; "2-AAAA" above "10-AAAA"; "1-BDDD" above "AAAA" (numbered first); "1-AAAA" above "1-AAAa"; "" last.

## 2. Overview when maximised: the details block never shows; text too big
Cause: the legend and fonts scale up to 1.7x and eat the space, and the block needs >= 70*ui px.
Fix in GlanceView.inc: cap the scale for text at 1.25 (`uiText`), keep the legend at 12.5-13.5 px, size bars with the full `ui`; work out the block's needed height first
(rows x line height) and give the top panel enough share (topH) when the window is tall; show the block whenever >= 3 lines fit, as many lines as fit. Make sure buildFacts really
returns data (log if empty; check lib->grades(v, 96000) returns the table and the per-version lines appear). Content = what the Full table's double-click dialog shows (MainGui.cpp
showDetails): every column's value (artist, album, year, genre, BPM, key, LUFS, peaks, length, format, directions, added ...), each Meter version's grade + values + directions,
BPM/key estimates with confidence, sample rate / bits / channels, possible duplicate. Two or three neat columns for the short facts, full-width lines for the per-version ones.
Verify with the app's snapshot (the CI GUI step screenshots views; make it also screenshot the Overview at 2000x1125 with a selected track and attach it to the artifact) and look at it.

## 3. Full table: "Back to overview" button covers the count text
LibraryTab places its toggle button over LibraryView's top-right; trim the count label's bounds by the button's width (or move the button into LibraryView's top row).

## 4. Add tracks: skipping files already in the library is slow
Cause (Engine.cpp Library::analyse): every file is fully read for its SHA256 id, and index() and grades(v) for every version are re-read from CSV for EACH file.
Fix: (a) a known-files cache in the library folder (`known-files.csv`: full path, size, modification time, id), written whenever a file is analysed or found already there;
before hashing, if path+size+mtime match a row whose id is complete (capture_version current and graded by every version) report AlreadyThere without opening the file.
(b) load index() and each grades(v) once per batch (cache in Library keyed by changeCount, or pass them in) instead of per file. Count and report "skipped (already in the library)".
Test in CI: add the same test files twice and check the second run says they were all skipped and is fast.

## 5. hintLAB: clearer bottom buttons and the low-end chart
- Main workflow buttons (bottom, in this order): "Set triggers from the library" + share box, "Export to meterDARK", "Back to meterDARK's values".
- Move "Level test" and "Measure low end" into the ADVANCED area (under the advanced constants, a sub-heading "CHECKS") with one-line captions:
  Level test: "checks the readings do not change when the same track is played quieter (they should not)";
  Measure low end: "checks the tone reading follows the real amount of low end in each track (needs the audio files; first run is slow, then cached)".
- The scatter area: only shown after a measurement; a title "Does the tone reading follow the real low end?" and a caption "each dot is a track: across = measured low-end
  share, up = tone reading. Dots rising left to right = it follows; r near 1 = closely, near 0 = not at all." When it cannot run (audio moved / no tracks with fine data) say
  why in that space instead of showing nothing.

## 6. gradeLAB: pick the Meter version
- analyser/tools/make_original_scorer.py: generate a `_tunable` copy for EVERY version (not only the newest); Versions.inc / OriginalScorer.cpp: `createTunableRun(versionId, ...)`,
  `tunableSettingsUsed(versionId)`, keep `tunableVersionId()` as the default (newest). Regenerate, never hand-edit src/gen.
- gradeLAB: a version box at the top ("Meter 0.5.5 / 0.5.6"); switching reloads that version's window data and original grades, rebuilds the sliders (only the settings that
  version's code has), clears lab results. Export to meterDARK writes that version's id (meterDARK only applies it when built on the same version; say so in the message).
- Tooltip on "Use replay blocks": "Score from each track's compact replay copy (the 16-bit window data inside its lean record) instead of the full stored window data. Needed for
  tracks shared with you in packs; for your own tracks it gives the same grades." 

## 7. Done
HOW-TO.txt (grade sort = by grade text like Finder, numbers as numbers, details block, fast skip, hintLAB checks, gradeLAB version box, replay blocks), HANDOFF.md LATEST section, push, notify.
