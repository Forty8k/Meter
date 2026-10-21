// meterDARK Analyser: the TRACK STORE. One in-memory picture of the library that every tab reads from.
//
// The library FOLDER stays the only real record. The store is filled from it once when the library is opened, and after that it is kept up to date one track at a time:
// the Library tells it exactly what it just wrote (see ChangeNote in Engine.h), and the store changes only that track. Nothing is read back from disk for a change.
// Nothing here is a "cache" in the throw-away-copy sense: the store is the app's working copy of the folder.
//
//  - index rows, your values, your columns and every Meter version's grades are kept in memory (small: a few MB for thousands of tracks)
//  - lean records (the Overview's bars) and window data (what the LABs replay) are read the first time somebody asks and then kept; a change to a track forgets only that track's copy
//  - every change is also written to a short numbered log, so a tab that polls `seq()` can ask "what changed since I last looked?" and update only that
//
// Threads: any thread may call any method. The store's own lock is never held while calling into Library, and Library calls the store only from inside its own lock, so the
// two cannot deadlock. Create the store BEFORE anything starts writing to the library (the constructor loads from disk, then starts listening).
#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include "Engine.h"

namespace mdark {

// one track's window data as a LAB replays it
struct WindowData { std::vector<std::string> names; std::vector<float> values; double sr = 96000.0; };

struct StoreChange {
    enum Kind { Added,     // a new track
                Regraded,  // an existing track was scanned or graded again (its window data changed too)
                Values,    // your values for a track changed (id empty = many at once)
                Removed,
                Columns,   // your custom columns changed
                Lean,      // a lean record was written (id empty = many)
                Shared,    // records from a pack were imported
                Reset } kind = Reset; // anything else: look at everything again
    juce::String id;
};

class TrackStore {
public:
    explicit TrackStore(std::shared_ptr<Library> library);
    ~TrackStore();
    const std::shared_ptr<Library>& library() const { return lib; }

    // ---- what changed. Poll seq() (cheap). changesSince fills `out` with everything after `since` and gives the new number in `now`;
    //      it returns false (and `out` is empty) when `since` is older than the log reaches: then look at everything again.
    uint64_t seq() const { return seq_.load(); }
    bool changesSince(uint64_t since, std::vector<StoreChange>& out, uint64_t& now) const;

    // ---- the library in memory (copies; the strings inside are shared, so a copy is cheap)
    Table index() const;
    int trackCount() const;
    bool indexRow(const juce::String& id, juce::StringArray& row, juce::StringArray* header = nullptr) const;
    std::map<juce::String, std::map<juce::String, juce::String>> userValues() const;
    std::map<juce::String, juce::String> userValuesOf(const juce::String& id) const;
    std::vector<CustomColumn> customColumns() const;
    Table nativeGrades(const juce::String& versionId) const;   // as the files have it
    Table readings(const juce::String& versionId) const;       // with the audio converted to 96 kHz; a track not rescanned yet: as the file has it (what the tabs show)

    // ---- lean records and shared records: read when first asked, then kept
    std::shared_ptr<const LeanRecord> lean(const juce::String& id);     // null when the track has none yet
    std::vector<juce::String> sharedIds();
    std::shared_ptr<const LeanRecord> shared(const juce::String& id);

    // ---- window data (what the LABs replay): per (Meter version, rate, from replay blocks) and track, read when first asked, then kept
    struct WindowKey {
        juce::String version; int rate = 0; bool fromBlocks = false;
        juce::String str() const { return version + "|" + juce::String(rate) + "|" + (fromBlocks ? "b" : "w"); }
    };
    std::shared_ptr<const WindowData> windows(const WindowKey& key, const juce::String& id);   // null when the track has no data for that key
    // reads the ones not kept yet, on several cores; returns how many were read from disk
    int loadWindows(const WindowKey& key, const std::vector<juce::String>& ids, std::function<void(int done, int total)> progress = {}, std::function<bool()> shouldStop = {});
    int windowsKept(const WindowKey& key) const;

private:
    void apply(const ChangeNote& n);
    void loadAll();
    void log(StoreChange::Kind k, const juce::String& id);
    void rebuildRowOf();
    std::shared_ptr<const WindowData> readWindows(const WindowKey& key, const juce::String& id);

    std::shared_ptr<Library> lib;
    mutable juce::CriticalSection lock;                // everything below except the window cache
    Table indexT; std::map<juce::String, size_t> rowOf;
    std::map<juce::String, std::map<juce::String, juce::String>> user;
    std::vector<CustomColumn> customs;
    struct GradeSet { juce::StringArray header; std::map<juce::String, juce::StringArray> nat, r96; };
    std::map<juce::String, GradeSet> grade;            // per Meter version
    std::map<juce::String, std::shared_ptr<const LeanRecord>> leanKept, sharedKept;   // a null pointer = looked, none there
    std::vector<juce::String> sharedList; bool sharedListKnown = false;
    std::deque<StoreChange> changeLog; uint64_t logStart = 0; std::atomic<uint64_t> seq_{0};

    mutable juce::CriticalSection windowLock;
    std::map<juce::String, std::map<juce::String, std::shared_ptr<const WindowData>>> windowKept;   // key.str() -> id -> data (null = none there)
};

} // namespace mdark
