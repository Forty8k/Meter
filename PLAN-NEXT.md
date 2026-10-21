# Next build (21 Oct 2026, round 3): the LABs never finish loading. Built by Sonnet, plan by Opus.
Read HANDOFF.md first. Branch `analyser` only. About 200-300 changed lines, mostly LabView.inc, a little MainGui.cpp / Engine.cpp.
Commit + push after each step, CI green before the next. When done: HOW-TO.txt + HANDOFF.md LATEST, push, SendUserMessage + PushNotification "LAB loading fixed and pushed", stop.

## What is going on (Opus, from the code)
- On every launch EACH LAB (hintLAB and gradeLAB separately) reads every track's stored window data, one file per track (`readCoreAt` also parses the track's meta.json
  each time). Nothing is cached between launches, and the two LABs do the same work twice and hold two copies in memory.
- LabView::timerCallback starts the whole load again whenever `lib->changeCount()` differs from the value taken when the load STARTED. Anything that changes the library
  while it loads (adding tracks: one bump per new file; the start-up update `upgradeOldRows`: one bump per track it updates, and it rewrites index.csv each time;
  building lean records; tag fill) makes it start over as soon as it finishes. On a 5000-track library that can go on for as long as the library keeps changing.
- Since the last fix, `rescore()` refuses while `loading` (correctly: before, scoring during a reload could crash), so the user now sees the endless reload as
  "The tracks are still loading" forever.

## 1. Never block on a RE-load; never restart a load because of a change
- First load only blocks scoring. After that, the loaded data stays usable: Re-score library / selected / preview / Set triggers / hints all work on it.
- A reload builds a NEW vector in the background; its callAsync swaps it in only if nothing is scoring (else keep it in `pendingTracks` and swap in the timer right
  after `finishRescore`). Swapping clears lab results as now.
- Debounce: when changeCount moves, note the time; start a reload only when the count has been unchanged for 5 s AND the tab is showing AND no load is running.
  `loadedChange` = the count when the reload STARTED (unchanged) - so a change during a load just leads to one more quiet-period reload later, not a loop.
- Status line while loading: "Loading tracks: N of M ..." (count in the job like loadDone/loadTotal, also when level mode is 0). Background reload: "Library changed;
  refreshing in the background (you can keep working) N of M". When done: "Loaded M tracks in X s" (so we learn the real time on his machine).
- Keep the existing guard that a version switch / replay-block switch (full replacement of what is loaded) blocks scoring until that load is done; say so in the status.

## 2. One load shared by hintLAB and gradeLAB
- A small `LabData` (tracks vector + key: library ptr, version id, rate, fromBlocks, level key, changeCount at start) held by MainComponent in a shared_ptr cache:
  `std::shared_ptr<const LabData> getLabData(key, progress)` - if the cached key matches, return it; else load once (one job) and both LABs get the same object.
  gradeLAB on the newest version without replay blocks uses the same data as hintLAB. LabView keeps `std::shared_ptr<const LabData> data` instead of owning `tracks`
  (or copies the pointer; Track is read-only after load, results stay per view).
- Memory: half of today.

## 3. Faster reading (measure first with the "Loaded M tracks in X s" line; do 3a always, 3b only if a 1000-track test library takes > 10 s)
3a. `readCoreAt`: take the track's sample rate from the index row (already in `load()`) instead of parsing meta.json; read meta.json only when the rate differs (for
    `rate_runs`). Pass the index rate in (new overload) so the common case (the file IS at 96 kHz, or rate 0) opens one file.
3b. A lab cache file per (version, rate): `labcache/<version>@<rate>.bin`, float32, one record per track (id, core file size + mtime, names, values), written after a full
    load, read first next launch; a track whose core file size/mtime differs (or is missing from the cache) is read from its own file. Not shared in packs; safe to delete.

## 4. CI
- tools test or CLI `--lab-load-test`: build a library of the 5 test tracks, time two loads (second must use the cache / shared data), and check a load started while
  `changeCount` keeps moving still finishes (simulate by bumping in a loop for 2 s).

## 5. Done
HOW-TO.txt: the LABs load once per launch (shared), keep working while the library changes, refresh in the background; HANDOFF.md LATEST; push; notify.
