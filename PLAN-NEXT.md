# Next build (21 Oct 2026, round 3, replaces the earlier round-3 plan): ONE in-memory track store for every tab. Built by Sonnet, plan by Opus.
Read HANDOFF.md first. Branch `analyser` only. Commit + push after each step, CI green before the next. Each step leaves the app working.
Size: about 700-900 changed lines in total, but the views lose most of their own loading code (net smaller). Engine.cpp/.h (store + change notes), MainGui.cpp
(Full table), GlanceView.inc (Overview), LabView.inc (both LABs).
When done: HOW-TO.txt + HANDOFF.md LATEST, push, SendUserMessage + PushNotification "Track store done and pushed", stop.

## Why (the user's point, 21 Oct)
Today every tab reads the library from disk on its own (Full table: index + user values + grade tables; Overview: every lean record; hintLAB and gradeLAB: every track's
window data, each separately) and throws it all away and starts again whenever ANYTHING in the library changes (one counter, `changeCount`). Renaming one track's genre
makes both LABs re-read thousands of window files. Adding tracks restarts them once per file.

## The design
- The library FOLDER stays the only real record (nothing about its files changes, packs are unchanged).
- `mdark::TrackStore` (new, Engine.h/.cpp or TrackStore.h/.cpp in MDARK_ENGINE_SOURCES): what is in the library, in memory, filled ONCE per launch and kept up to date
  track by track. One per open library, owned by MainComponent next to `library`, handed to every tab (shared_ptr). Thread safe (a lock; readers take a snapshot of
  the rows they need or hold a `std::shared_ptr<const Track>`).
  Per track (by id): the index row, your values, each Meter version's grade row (native, @44100, @96000), the lean record (bars, tags), and a `WindowsSlot` per
  (version, rate) that is EMPTY until a LAB first asks for it, then filled once and kept. Shared records (lean-shared) are tracks too, marked shared.
  Text and grades for 5000 tracks are a few MB; window data is the big part and is only loaded for what a LAB actually uses.
- Change notes instead of one counter: the Library reports WHAT changed - `added(id)`, `regraded(id)` (new grades / rescan), `valuesChanged(id)` (your values, tags filled),
  `removed(id)`, `columnsChanged()`, `sharedImported(ids)`, `leanBuilt(ids)`. Library calls a listener (set by the store) at each of the places that today do `++changes`;
  `changeCount` stays for anything not converted yet. The store applies the note to that one track only (re-reads just that track's files), then tells the tabs
  (juce::ChangeBroadcaster-style, on the message thread) with the list of ids and the kind.
- Tabs react to the kind, never by reloading everything:
  values changed  -> repaint/re-filter in memory (Full table row, Overview name/columns, LAB search text); no window data touched.
  added           -> the new track is appended: a new row in the Full table and Overview; the LABs load ITS windows only and add it (its lab result is empty until scored).
  regraded        -> that track's grade text/ranks update; LAB windows for that track re-read.
  removed         -> drop that row everywhere.
  filters changed -> (already in memory) re-filter only.
- Results in the LABs are kept per track id (std::map or vector indexed by store position that only grows), so adding a track never clears anyone's results.
- Word: "cache" is only right for a copy on disk that can be thrown away. The in-memory thing is the "track store" (or library model); the disk copy (step 5, if needed)
  is the "start-up cache".

## Steps
1. TrackStore + change notes in the engine; fill it once at library open (index, user values, grades, lean records; several cores); CLI `--store-test`
   (open the test library, check counts; add a track and check only that id was read; set a user value and check no window slot was touched). No tab uses it yet.
2. Full table (LibraryView) reads rows from the store; its own index/grades/user loading goes; edits go through Library as now and arrive back as valuesChanged(id).
3. Overview (GlanceView) reads Items from the store (built from the lean record + values); `gather()` goes; added/removed/values update the list in place.
4. LABs: `store->windows(version, rate, fromBlocks)` returns the slot, loading only missing tracks (progress "Loading tracks: N of M", then "Loaded M tracks in X s"),
   shared by hintLAB and gradeLAB (gradeLAB on 0.5.5 gets its own slot). LabView no longer owns or reloads `tracks`; it keeps the
   slot pointer + its own results by id. No reload on changes: added -> append; regraded -> re-read that one. Scoring takes a snapshot of the shared_ptrs it uses, so
   a track appended mid-scoring is simply not part of that run (no more "still loading" block except the very first load).
   The level mode (normalised/mix) grades: kept per slot like today's levelGrades, computed for missing ids only.
5. Only if step 4's "Loaded M tracks in X s" is slow on his library (> ~20 s): a start-up cache file per window slot (`labcache/<version>@<rate>.bin`, float32,
   per track: id, core file size+mtime, names, values), read first, appended when tracks are added, a track re-read only when its core file changed. Safe to delete.
6. Remove the dead code (per-tab loaders, changeCount polling in the tabs), docs, push, notify.
