# Next build: Analyser steps 1-5 (Sonnet), then step 6 (Opus)

Written 20 Oct 2026 for the model that builds it. Read HANDOFF.md first for the rules and the local compile check.

## Rules (unchanged)
- Explain plainly to the user. Never merge into `main`. Never hand-edit generated files (`source/chris/*`, `analyser/src/gen/*`). Chris's code is never edited.
- Every new setting at 0/off = stock behaviour. No "f8k" naming. No public releases (release job stays `if: false`). No macOS build.
- Analyser work is on branch `analyser` in `/root/Meter` (folder `analyser/`). Plugin is branch `meterdark` in `/root/Meter-plugin`.
- Local syntax check (from `/root/Meter/analyser`):
  `g++ -std=c++17 -fsyntax-only -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DJUCE_USE_CURL=0 -I ../JUCE/modules src/MainGui.cpp`
  (same without the X flags for Engine.cpp / MainCli.cpp). Then push and watch CI: `gh run list --branch analyser --limit 1` (run gh from inside /root/Meter).
- Commit after each step (message ends with the Co-Authored-By / Claude-Session lines from the system reminder). Push; CI must be green before moving on.
- `.inc` files are included inside MainGui.cpp's anonymous namespace: put any new JUCE module includes at the top of MainGui.cpp.

## Build order: 2, 1, 4, 5, 3 (numbers are the user's)

### Step 2 - bigger time / "who leads" readout, in the header row
File: `analyser/src/GlanceView.inc` (top-panel paint, the block commented "what leads where the playhead is").
- Move the readout (e.g. "1:02   detail leads") out of its own line under the small bars into the head row, between the track name (left) and the grade (right), centred, bold, about the title's size (scale with step 1).
- Keep: yellow (kAccent), live during playback (timer already repaints `topArea` every 80 ms), follows the mouse when not playing, "balanced" when nothing leads.
- When nothing is playing and the mouse is not over the bars, show nothing there (drop the grey "Hover over the bars..." hint, or put it small under the bars).
- The freed line under the small bars goes back to the layout.

### Step 1 - overview scales with the window, plus a details block
File: `GlanceView.inc`.
- One scale factor, e.g. `const float ui = jlimit(1.0f, 1.6f, getHeight() / 800.0f)` (tune so the user's ~1350x800 window looks exactly as now = 1.0, a maximised 2000x1125 window ~1.4). Apply it to: title font, grade font, technical details line, the readout from step 2, bar label text and chips, legend text and spacing, small bar height (24 -> 24*ui). Big bar height rule stays (it was approved).
- `topH` (the top panel share) may grow a little with height so the new block fits; the list keeps at least ~45% of the height.
- DETAILS BLOCK under the legend (only when there is room; hide it when the space left is under ~90 px): the information the Full table's double-click dialog shows (`showDetails` in MainGui.cpp ~line 870), laid out neatly in two or three columns, label dim / value bright:
  artist, album, year, genre, BPM (+ estimate confidence), key (+ confidence), format, lossy, sample rate / bits / channels, LUFS, peak, true peak, length, source, added date;
  per Meter version: grade, authority / power / detail values, letter value, bonus value, directions ("more calm (75%) most of the loud parts, 'allow fullness' at the end").
  Data: the lean record meta (`it.rec.get(...)`) has most of it; for the per-version grades and the index fields use the library (`lib->index()`, the grade tables). If those are not easy to reach from GlanceView, add a small helper on `mdark::Library` (Engine.h/.cpp) that returns the per-version grade rows for an id, and cache the result per selected id (do not re-read files in paint).
- Must look the same as now at the user's normal size (screenshot 1 of 20 Oct) and fill the space when maximised (screenshot 2).

### Step 4 - Full table: "Apply filters" toggle
File: `analyser/src/MainGui.cpp` (LibraryView: `rebuild` around lines 600-635 builds `shown`; `labSet()` ~757 is the set the Overview and LABs use; `Filters.inc` holds the filter state).
- Add a toggle button "Apply filters" in the Full table's toolbar, ON by default, remembered (store in the library folder, e.g. `fulltable-filters.txt`, or wherever the other view settings live).
- ON: the Full table shows exactly the tracks the Overview / LABs use (`labSet()`; including "hide duplicates, prefer the highest quality version"). OFF: everything, as now.
- The count label reads e.g. "2,467 of 3,694 tracks (filters on)" when filtering.
- Check that the duplicate highlight, sorting, editing and the right-click menu still work in both modes.

### Step 5 - gradeLAB quick preview
File: `analyser/src/LabView.inc` (mode 1 = gradeLAB; `rescore()` ~line 390 uses the ThreadPool `pool`; `finishRescore()`).
- Add a "Preview" tick (on by default) next to "Re-score library".
- Preview ON: about 400 ms after a slider stops moving, regrade a fixed SAMPLE of tracks: ~150 (or all if fewer), chosen once per library load to cover the grade range (sort the in-use tracks by their real grade key and take every Nth). Cancel/ignore a running preview when a new one starts (generation counter; jobs check it and skip).
- Status line after a preview: "Preview on 150 tracks: 37 grades changed, ranking kept 92% (Spearman r = 0.96). Press Re-score library for the full result."
- Table: preview rows show their new grade; rows not in the sample show "-" in the lab-grade column (or keep the previous full result greyed). "Re-score library" stays the exact full run and replaces the preview.
- Do NOT change the scoring code itself.

### Step 3 - "Fill missing tags..." (folders, then MusicBrainz + Discogs)
New file suggested: `analyser/src/TagFill.h/.cpp` (engine side, no GUI) + a dialog in MainGui.cpp (Full table toolbar button "Fill missing tags...").
Writes ONLY to the user's edits (`Library::setUserValue(id, key, value)`, keys `artist`, `album`, `year`, `genre`), never to audio files, never overwrites a non-empty field (tag or user value).
Every value it writes is also listed in `tagfill-log.csv` in the library folder (id, field, value, source = folder / musicbrainz / discogs, time) so "Undo last fill" can remove exactly those user values.
1. FROM FOLDERS (instant, offline): use `source_path` from the index. Rules:
   - a folder named like a known genre (Rock, Pop, Jazz, Hip Hop / Hip-Hop / Rap, Electronic, Dance, House, Techno, Drum and Bass / DnB, Reggae, Soul, Funk, R&B / RnB, Blues, Country, Folk, Metal, Punk, Indie, Classical, Soundtrack / OST, Disco, Ambient, Latin, World, Gospel, Alternative ...; case-insensitive, compare on letters only) -> genre (nearest such folder above the file wins).
   - "Artist/Album/track" layouts: if artist is empty and the parent-of-album folder is not a genre name, use it as artist; album folder -> album when empty.
   - a 4-digit year 1900-2030 in an album folder name like "(1997)" / "[1997]" / "1997 - Album" -> year when empty.
2. MUSICBRAINZ (online, no account): for tracks still missing year or genre and that have artist + title. `https://musicbrainz.org/ws/2/recording/?query=recording:"<title>" AND artist:"<artist>"&fmt=json&inc=genres+tags+releases` (check the current API docs; use `inc` only where allowed, or a second lookup on the release group). Year = earliest `first-release-date` among matching releases (score >= 90). Genre = highest-count genre (fall back to tags). Strip "(Remastered 2009)", "- 2015 Remaster", "(feat. ...)" from titles before searching.
   - REQUIRED: a User-Agent header like `meterDARKAnalyser/1.1 ( hiddenfeatures@gmail.com )` is NOT allowed to contain the email in code we publish - use `meterDARKAnalyser/1.1 ( https://github.com/Forty8k/Meter )`. Max 1 request per second (sleep between requests). Retry once on 503.
3. DISCOGS (online, optional): only when the user has pasted a personal token (a text box in the dialog, saved to `discogs-token.txt` in the app's settings folder, not the library folder, since libraries can be shared). `https://api.discogs.com/database/search?artist=...&track=...&type=release&token=...` with the same User-Agent; max ~1 request per second. Genre: Discogs `genre[0]` (and offer its first `style` as a "style" user column only if trivially easy; otherwise skip). Year: the earliest `year` among results. Discogs genre is preferred over MusicBrainz genre when both exist; folder genre beats both.
- Runs on a background thread with a progress line and a Stop button; can be stopped and resumed (skip ids already in the log). Make the network calls with `juce::URL::createInputStream` (Windows has WinINet; nothing to add). Cache raw answers per artist+title in `tagfill-cache.json` so a re-run is free.
- Dialog: three ticks (Folders / MusicBrainz / Discogs), the token box, Start, Stop, "Undo last fill", and a summary at the end ("Filled: 812 genres, 640 years, 95 artists. Nothing was overwritten.").
- This workspace cannot reach musicbrainz.org or api.discogs.com: test the parsing with saved sample JSON (write a couple by hand from the API docs into `analyser/tools/tagfill-samples/`) and a small CLI self-test, and test the folder rules on made-up paths. Say clearly in the handover that the online part was not run against the live services.
- Optional CLI: `--fill-tags [--folders-only]` for scripted use.

## When steps 1-5 are done
1. Update `analyser/HOW-TO.txt` for every change.
2. Update `HANDOFF.md`: replace the "LATEST" section with what was built (per step: files, how it works, what was verified, what was NOT verified and needs the user's hand check, known limits). Add a section "FOR OPUS: STEP 6" (copy the step 6 text below and add anything you learned, e.g. where the plugin's tuned scoring copy lives).
3. Commit and push (CI green), then tell the user with SendUserMessage AND a PushNotification: "Steps 1-5 are done and pushed. Switch the model back to Opus and say go for step 6." Then stop.

## Step 6 (OPUS, not Sonnet) - gradeLAB settings to meterDARK
- Analyser: gradeLAB gets "Export to meterDARK" writing `<app data>/meterDARK/grade-tuning.xml` (the ScoreParams, the Meter version id they were tuned for, an export time).
- Plugin: its own tuned copy of Chris's scoring (the one the "grade" tick uses; PluginEditor.cpp `gradeTuned`, AirwindowsUI tune[12]) reads those constants live (same reload pattern as `LabTuning` in AirwindowsUI.h). Chris's code is never edited. Only applies when meterDARK's Meter version equals the exported version id; otherwise stock and a dim note.
- When active: "gradeLAB" shows yellow on the bottom row next to "hintLAB", the grade shows `*`. Clicking "gradeLAB" switches it off (back to stock) for this instance; clicking "hintLAB" likewise switches the exported hint constants off.
- At 0/off = stock behaviour.
