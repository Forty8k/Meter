# Next build (22 Oct 2026, round 4): share-pack fixes + Chris's Meter 0.5.7. Plan by Opus, built by Sonnet.
Read HANDOFF.md first. Two branches: `analyser` (repo /root/Meter) and `meterdark` (worktree /root/Meter-plugin). Never main.
Commit + push after each part, CI green before the next. Generated files only via their scripts. Chris's code never edited.
When done: HOW-TO.txt + HANDOFF.md LATEST (both branches where they apply), push, SendUserMessage + PushNotification "Packs + 0.5.7 done and pushed", stop.

## Part A: share packs (analyser branch)
What the user saw on his second machine: imported a full pack, the names were "random characters", then every shared record vanished, and a second import showed nothing.
Causes found in the code (Opus, 22 Oct):
1. NAMES. `Library::exportPack` strips file / path / filename (kPrivateKeys) and keeps only the lean record's artist/title meta. His files mostly have no title tag
   (names come from file names), and lean meta tags are only refreshed by "Build / update records". So the receiver shows the record id (a hash) = "random characters".
2. VANISHING. `GlanceView::gather` adds shared records only when `getAllowed()` is null, i.e. only while "Lab uses filtered tracks" is OFF. With it on (kept per library in
   filters.json; may have been on, or switched on by loading filters, just after the import) every shared record is hidden, and the Full table never lists shared records at all. That also explains "import a second time shows nothing": the
   import works (it overwrites lean-shared/<id>.lnr.gz and says "N records added"), the Overview just hides them.
3. A real race in TrackStore (from the store round): `sharedIds()`, `shared()`, `lean()` and `windows()` read the disk OUTSIDE the lock and then cache the result. If a change
   (SharedAll, LeanOne, GradeRow) clears the cache while that read is running, the old result (e.g. an empty shared list) is cached afterwards and stays until the next change.

Fixes:
A1. Pack names. exportPack writes a "name" meta key = exactly what the Overview shows for that track (artist - title from the CURRENT values: user.csv values win over index
    tags win over lean meta; else the file name WITHOUT folder or extension). Also refresh artist/title/album/year/genre in the exported copy from the current values.
    Share menu: "Export a display pack..." / "Export a full pack..." each ask once: "Include track names?" (Yes = names as above; No = anonymous, as today). Default Yes.
    Paths / folders are never written (kPrivateKeys stays; "name" is not in it). Import keeps "name". Overview / LAB name for a shared record: rec "name" if present, else
    artist - title, else "Shared track " + first 6 characters of the id. (Old packs have no names: re-export from the first machine.)
A2. Shared records always show in the Overview, marked "(shared)", whatever the filters (filters only judge your own tracks). Overview status "N tracks + M shared".
    A "Shared: shown / hidden" toggle next to "Share packs..." (saved in the library's overview settings) for when he doesn't want them.
A3. Import message: "N new, M updated (already there), K skipped because you measured them yourself, B could not be read." After an import the Overview reloads and
    scrolls to the first new one.
A4. TrackStore race: an `epoch` counter bumped (under the lock) every time a cache is cleared/invalidated; each reader takes the epoch under the lock before the disk
    read and stores its result only if the epoch is unchanged (otherwise returns it uncached). Same pattern for sharedIds, shared, lean, windows, loadWindows.
A5. CI: the existing pack step (analyser.yml ~line 164) also checks: the pack contains the "name" for each record (`--pack-names <file>` CLI prints them; grep a known
    test name), an anonymous pack (`--export-pack ... --no-names`) contains none, importing twice reports "0 new, N updated", and `--store-test` gains: import a pack
    into the test library -> store->sharedIds() count and names are right.

## Part B: Meter 0.5.7 in the Analyser (analyser branch)
Upstream https://github.com/airwindows/meter commit e0faab5 (22 Oct, "Earsplitting highs are not hit material", VERSION 0.5.7). Only source/AirwindowsUI.cpp changed:
 - the detail black line (departureSlew) gets thinner when the beam is near the top (beamSlew > 186) and collapses to (199 - beamSlew) above 198;
 - "directions" now only show when there is NO bonus number (before: always, faded by balance).
PluginProcessor.* and PluginEditor.* are byte-identical to 0.5.6 -> the captured WINDOWS are identical; only the scoring (paint) differs.
Opus dry-ran both generators with the 0.5.7 source added: both accept it; the tunable copy keeps all 18 gradeLAB settings.

B1. Add `analyser/original/0.5.7-e0faab5/` (AirwindowsUI.cpp/.h, PluginEditor.cpp, PluginProcessor.cpp/.h from that commit, upstream.json like the others) and run
    `analyser/tools/make_original_scorer.py`. 0.5.7 becomes the newest = tunableVersionId() (hintLAB/gradeLAB default, 96 kHz readings, lean record grade).
B2. "Measures the same" (the important part; avoids an audio rescan of 5000 tracks): the generator compares each version's extracted capture part (message types +
    processBlock + the editor's message switch) with the older versions and writes `sameWindowsAs` (the oldest identical version id, empty if none) into
    MeterVersion (Versions.inc). Engine: `readCore` / `readCoreAt` / rate_runs lookups for a version with no files of its own fall back to its twin's files.
    `upgradeOldRows` then fills 0.5.7 for every track by REPLAYING the stored windows (native rate AND 96000 for the newest, so the Library's readings exist),
    no audio needed. New scans: a twin version is not captured again; its grades come from replaying the twin's windows (saves a third of the scan time).
B3. Batch the grade writes in upgradeOldRows: today each `putGradeRow` loads + saves the whole grades CSV, so 5000 tracks x 2 rates = 10000 full rewrites. Collect the rows
    per (version, rate) and write once per 200 tracks and at the end (one ChangeNote per track still, so the store/tabs update).
B4. Library opens -> the worker's upgrade adds 0.5.7 (status "Adding Meter 0.5.7 grades from stored windows: N of M"). "Build / update records" then refreshes lean
    records to the 0.5.7 grade (replay blocks unchanged). Version boxes list 0.5.7 automatically; keep the user's saved grade/compare choice.
B5. gradeLAB: settings exported for 0.5.6 do not apply to a meterDARK built on 0.5.7 (by design, meterVersion in grade-tuning.xml). Say so in HOW-TO.
B6. CI: selftest / --store-test still pass; add a check that a test track's 0.5.7 grade from the twin windows equals scanning the audio with 0.5.7 directly
    (`--twin-test`, tiny generated WAV). Also check 0.5.7 directions are empty whenever a bonus number exists.

## Part C: Meter 0.5.7 in meterDARK (meterdark branch, /root/Meter-plugin)
C1. Add `chris/0.5.7-e0faab5/` (same five files + upstream.json), run `tools/make_chris_layer.py` -> Chris_v0_5_7_e0faab5.h, Current.h now on 0.5.7. Keep 0.5.5 and 0.5.6.
    Make the generator also write `constexpr int kChrisVersion = 507;` (major*10000+minor*100+patch style is fine, just consistent) into Current.h.
C2. meterDARK draws Chris's lines itself (source/AirwindowsUI.cpp ~line 471, the `hasZeroRmsLines` block copies his departureSlew formula). When kChrisVersion >= 0.5.7:
    apply his new rule to the slew line's thickness, using HIS unscaled beam (sqrt(dataBL+dataBR)*618.033988749894f, capped 199; not meterDARK's zoomed / curved `beam`):
    departure = fmin(6.18033988749894f/(1+fmax(chrisBeam-186,0)*0.2f), <old expression>); if (chrisBeam > 198) departure = 199 - chrisBeam.
    The yellow "static" look and the 2 px minimum stay meterDARK's (so a collapsed line still shows, yellow). Scoring comes from Chris's code untouched.
C3. meterDARK does not draw "directions" today (checked: no use in source/); nothing to do. VERSION file -> 0.5.7 if it only mirrors Chris's version (check CMake use first).
C4. The gradeLAB key list in make_chris_layer.py must still match the analyser's RULES (both scripts print the keys they used; CI compares as today).
C5. CI green on meterdark (cmake_ctest.yml). HANDOFF.md (plugin) LATEST: built on 0.5.7, what changed.
