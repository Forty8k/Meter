# Next build (21 Oct 2026, round 2): search in the LABs. Built by Sonnet, plan by Opus.
Read HANDOFF.md first (rules, local compile check, CI). Branch `analyser` only. Small round: about 60-80 changed lines, all in analyser/src/LabView.inc.
When done: update HOW-TO.txt + HANDOFF.md LATEST, push, CI green, SendUserMessage + PushNotification "LAB search done and pushed", stop.

## Background (checked by Opus)
hintLAB and gradeLAB already USE the filtered tracks: `getAllowed` = `LibraryView::labIds()` (same set as the Overview) when "Lab uses filtered tracks" is on;
`refreshUse()` builds the `use` mask; triggers (line ~294), hint readings (`useCopy`), scoring, preview and ranks all skip tracks not in use.
BUT the results table (`Model::refresh`, ~line 687) still LISTS every track: the filtered-out ones show with empty cells. That looks like the filters are ignored.

## 1. LAB tables list only the tracks in use
In `Model::refresh` build `order` only from tracks where `owner->inUse(i)`. The count shows "N tracks" (or "N of M tracks" while searching, see 2).
Selected row: keep it by track id if it is still listed (as GlanceView::resort does).

## 2. A search box in hintLAB and gradeLAB (view only)
- A `juce::TextEditor search` in LabView's top row, right side (width jlimit(150, 260, width/4)), same look and placeholder as the Full table's ("Search name, artist, genre, key ...").
- One lowercase haystack per Track, built in `load()`: file name, and from the index row + user values: artist, title, album, year, genre, bpm, key, source, and any value of yours
  (lib->userValues()); for shared records the lean record's artist/title/album/year/genre/source.
- Rule: every word must appear (same as Full table / Overview). Filter applied in `Model::refresh` on top of 1. `search.onTextChange` -> `model.refresh()`.
- IMPORTANT: the search only narrows what the TABLE shows. It never changes which tracks set the triggers, get scored, previewed or ranked (that stays the Library's filters),
  so typing a search never starts a recompute. Tooltip: "Narrows the list below. The LAB still works from the Library's filters; searching changes nothing it calculates."
- hintLAB's distribution chart (`dist`) is unchanged (the whole filtered set).

## 2b. Overview: "Overlay lines" on by default
GlanceView.inc constructor: `linesButton.setToggleState(false, ...)` -> true (the lines now sit inside the small tone/punch/detail bars, not over the big bar). One line.

## 3. Done
HOW-TO.txt (LAB lists only filtered tracks; search box = view only), HANDOFF.md LATEST, push, notify.
