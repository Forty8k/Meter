// meterDARK Analyser: reads an audio file, captures it (Capture.h), replays the original scoring (OriginalScorer.h)
// and keeps the results in a library folder. Shared by the window app and the command-line tool.
//
// The library has two kinds of data, kept apart:
//  MEASURED  index.csv + tracks/*  written only by scanning. Never edited by hand.
//  YOURS     user.csv + columns.json  your tag fixes, BPM/key corrections and your own columns. Scanning never touches these.
#pragma once
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <map>
#include <set>
#include "Capture.h"
#include "OriginalScorer.h"
#include "Resampler.h"
#include "LeanRecord.h"

namespace mdark {

// A small CSV table addressed by column name.
struct Table {
    juce::StringArray header;
    std::vector<juce::StringArray> rows;
    int col(const juce::String& name) const { return header.indexOf(name); }
    juce::String get(size_t row, const juce::String& name) const { const int c = col(name); return (c >= 0 && c < rows[row].size()) ? rows[row][c] : juce::String(); }
    void set(size_t row, const juce::String& name, const juce::String& v);
    int findRow(const juce::String& key, const juce::String& value) const;
    bool load(const juce::File& f);
    bool save(const juce::File& f) const; // written to a temporary file, then swapped in
    static std::vector<juce::StringArray> parse(const juce::String& text);
};

struct AnalyseOutcome {
    enum Status { Added, Updated, AlreadyThere, Failed } status = Failed;
    juce::String id, message;
};

struct CustomColumn { juce::String key, name; bool numeric = false; };

// How loud the audio is taken to be when a track is graded (the same three modes as meterDARK): 0 original = as the file is; 1 normalised = a fixed gain from
// "input at" to "score as if at"; 2 mix = a floating gain that keeps the running peak ceiling at -1 dBFS (the plugin's maths).
struct LevelMode {
    int mode = 0; double inLufs = -14.0, asLufs = -11.0;
    juce::String key() const { return mode == 1 ? "N" + juce::String(juce::roundToInt(inLufs)) + "_" + juce::String(juce::roundToInt(asLufs)) : mode == 2 ? juce::String("M") : juce::String(); }
    juce::String suffix() const { return mode == 1 ? "N" : mode == 2 ? "M" : ""; }
    double gain() const { return std::pow(10.0, (asLufs - inLufs)/20.0); }
    bool operator==(const LevelMode& o) const { return key() == o.key(); }
    bool operator!=(const LevelMode& o) const { return !(*this == o); }
};

// What changed in the library, told to whoever listens (the TrackStore) at the moment it is written, with the data itself, so nothing has to be read back from disk.
struct ChangeNote {
    enum Kind { IndexRow,      // a track's row in index.csv was added or replaced: id, header, cells
                GradeRow,      // a track's grade row of one Meter version (and rate, 0 = as the file has it) was written: id, version, rate, header, cells
                UserValue,     // one of YOUR values: id, key, value ("" = removed)
                Removed,       // a track was removed: id
                Columns,       // your custom columns changed
                LeanOne,       // one lean record was written: id
                SharedAll,     // records from a pack were imported
                ReloadUser,    // user.csv was rewritten in bulk
                ReloadAll } kind = ReloadAll;
    juce::String id, version, key, value; int rate = 0;
    juce::StringArray header, cells;
};

class Library {
public:
    explicit Library(const juce::File& folder);
    juce::File folder;
    std::function<void(const ChangeNote&)> onChange; // set by the TrackStore; called on whatever thread wrote the change, with the library's lock held (keep it short, never call back into Library from a different thread while holding your own lock)

    // scanning (any thread)
    // A file already in the library (same path, size and modification time as when it was added, and complete) is skipped without being opened:
    // known-files.csv in the library folder remembers path, size, modification time and id. Everything else is hashed, then looked up.
    AnalyseOutcome analyse(const juce::File& audio, std::function<void(double)> progress = {}, std::function<bool()> shouldStop = {});
    // Fills in what later versions added: grades from Meter versions a track has not been scored by yet (replayed from its
    // stored window data, or scanned again from its audio if that version measures differently), and BPM. Uses several cores.
    int upgradeOldRows(std::function<bool()> shouldStop = {}, std::function<void(int done, int total, const juce::String& file)> progress = {});
    // Scans again every track captured by an older analyser (capture version below the current one) from its stored source_path.
    // Each finished track is committed at once, so stopping and starting again carries on where it stopped. Files that are gone are
    // listed through `log` and left as they are. Your own values (user.csv) are never touched. Uses several cores.
    int rescanLibrary(std::function<bool()> shouldStop = {}, std::function<void(int done, int total, const juce::String& file)> progress = {},
                      std::function<void(const juce::String&)> log = {});
    int tracksNeedingRescan(); // how many tracks rescanLibrary would scan
    struct Lane; // one way of presenting a track's audio to the Meter versions (as the file has it, or converted to an anchor rate)
    int lastNoAudio = 0; // tracks whose audio was needed but could not be found (moved or deleted)
    static bool isAudioFile(const juce::File& f);
    static void collectAudioFiles(const juce::File& fileOrFolder, juce::Array<juce::File>& out);

    // reading and editing (any thread, all locked)
    Table index();                                      // a copy of index.csv
    // A copy of grades-<version>.csv: one Meter version's verdict on every track. rate = 0: the audio as the file has it.
    // rate = 44100 or 96000: the same audio converted to that rate first (tracks that already are at that rate answer from their own grades).
    Table grades(const juce::String& versionId, int rate = 0);
    Table gradeFile(const juce::String& versionId, int rate = 0); // exactly what grades-<version>[@rate].csv holds (no merging); the TrackStore reads this once
    // The same, graded in a level mode by replaying the stored windows with the mode's gain (no audio needed). Results are kept in levelgrades-*.csv and only
    // missing or out-of-date tracks are worked out (several cores). compute = false returns what is already kept and counts the rest in *missing.
    Table levelGrades(const juce::String& versionId, int rate, const LevelMode& lm, bool compute, std::function<bool()> shouldStop = {},
                      std::function<void(int done, int total)> progress = {}, int* missing = nullptr);
    static const std::vector<int>& anchorRates();       // the converted rates every track is also scanned at: 44100, 96000
    static std::vector<juce::String> versionIds();      // every Meter version the analyser has, oldest first
    std::map<juce::String, std::map<juce::String, juce::String>> userValues(); // id -> key -> value
    std::vector<CustomColumn> customColumns();
    void setUserValue(const juce::String& id, const juce::String& key, const juce::String& value);
    struct UserEdit { juce::String id, key, value; };
    void setUserValues(const std::vector<UserEdit>& edits); // many at once (one read and one write); an empty value removes the edit
    juce::String addCustomColumn(const juce::String& name, bool numeric);   // returns its key
    void renameCustomColumn(const juce::String& key, const juce::String& newName);
    void removeCustomColumn(const juce::String& key);
    bool removeTrack(const juce::String& id);
    // Import a CSV of your own values. It needs an "id" or "file" column to match tracks (file names match without
    // extension and case). Columns named like an editable column (artist, title, album, year, genre, bpm, key or one of
    // yours) fill that column; any other column becomes a new column of yours.
    juce::String importCsv(const juce::File& csv);

    static const juce::StringArray& editableBuiltIns();   // artist, title, album, year, genre, bpm, key
    int changeCount() const { return changes.load(); }  // goes up whenever anything in the library changes

private:
    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
    std::atomic<int> changes{0};
    void notify(const ChangeNote& n) { ++changes; if (onChange) onChange(n); }
    juce::File indexFile() const { return folder.getChildFile("index.csv"); }
    juce::File userFile() const { return folder.getChildFile("user.csv"); }
    juce::File columnsFile() const { return folder.getChildFile("columns.json"); }
    // what is already in the library, kept in memory so a batch does not re-read the CSV files for every file
    struct DoneCache { int change = -1; std::map<juce::String, bool> extrasCurrent; std::map<juce::String, std::set<juce::String>> gradeIds; } doneCache;
    struct KnownFile { juce::int64 size = 0, mtime = 0; juce::String id; };
    std::map<juce::String, KnownFile> known; bool knownLoaded = false;
    juce::File knownFile() const { return folder.getChildFile("known-files.csv"); }
    void refreshDoneCache();                       // call with the lock held
    bool isComplete(const juce::String& id);       // call with the lock held, after refreshDoneCache()
    void loadKnown();                              // call with the lock held
    void rememberKnown(const juce::File& audio, const juce::String& id);
    AnalyseOutcome analyseHashed(const juce::File& audio, std::function<void(double)> progress, std::function<bool()> shouldStop);
    void writeReadme();
    void saveColumns(const std::vector<CustomColumn>& cols);
    void putIndexRow(const juce::StringArray& cells); // add, or replace the row with the same id
    void putGradeRow(const juce::String& vid, const juce::String& id, const OriginalResult& r, int rate = 0);
    juce::File gradesFile(const juce::String& vid, int rate = 0) const;
    juce::File levelGradesFile(const juce::String& vid, int rate, const LevelMode& lm) const;
    juce::File rateCoreFile(const juce::String& id, const juce::String& vid, int rate) const;
    juce::File coreFile(const juce::String& id, const juce::String& vid) const;
    bool scanAudio(const juce::File& audio, Capture* cap, std::vector<Lane>& lanes, std::function<void(double)> progress,
                   std::function<bool()> shouldStop, juce::String& error, std::unique_ptr<juce::AudioFormatReader>* readerOut);
    bool scanVersions(const juce::String& id, const juce::File& audio, const std::vector<juce::String>& vids, std::function<void(double)> progress, std::function<bool()> shouldStop);
public:
    bool readCore(const juce::String& id, const juce::String& vid, std::vector<std::string>& names, std::vector<float>& values);
    // The window data of the newest Meter version at a converted rate (44100 / 96000). For a track already at that rate this is its normal core.
    bool readCoreAt(const juce::String& id, const juce::String& vid, int rate, std::vector<std::string>& names, std::vector<float>& values);
    // How much of each track's real energy sits in the low end (the three lowest octave bands, 31-125 Hz, as a share of all ten), from the fine blocks.
    // Cached in lowend.csv; only tracks not in the cache are read. Used by hintLAB to test whether the weight reading follows the real low end.
    // THE LEAN RECORDS: one small shareable file per track in lean/<id>.lnr.gz (display bars, grade values, tags and the packed replay block; see LeanRecord.h),
    // built from the window data already stored (no audio is read). Fine blocks, pitch, file paths and your own columns are not in it. Several cores.
    struct LeanReport { int built = 0, kept = 0, noData = 0, mismatches = 0; double bytesAll = 0, bytesReplay = 0; int totalTracks = 0; };
    LeanReport buildLeanRecords(bool force, bool verify, std::function<bool()> shouldStop = {}, std::function<void(int, int)> progress = {});
    bool readLean(const juce::String& id, LeanRecord& out);
    // SHARE PACKS: one file (.mdarkpack) of lean records. A display pack has no replay blocks (small: just the bars, grades and tags); a full pack keeps them so the
    // receiver can regrade. Never any file path, file name or private column; the only identity is the record's id. Imported records go to lean-shared/ and are
    // never mixed into your own measurements.
    int exportPack(const juce::File& out, bool full, const std::vector<juce::String>& ids);
    juce::String importPack(const juce::File& in, int* added = nullptr);
    std::vector<juce::String> sharedIds();
    bool readShared(const juce::String& id, LeanRecord& out);
    bool writeLean(const juce::String& id, const LeanRecord& rec); // gzip and store
    bool setLeanMeta(const juce::String& id, const std::vector<std::pair<std::string, std::string>>& changes); // patch tags / source in an existing record (no window data needed)
    juce::File leanFile(const juce::String& id) const { return folder.getChildFile("lean").getChildFile(id + ".lnr.gz"); }
    std::map<juce::String, double> lowEndShares(std::function<bool()> shouldStop, std::function<void(int, int)> progress);
private:
};

juce::StringPairArray readFlacTags(const juce::File& f); // Vorbis comments (TITLE, ARTIST, DATE, GENRE ...), JUCE does not read these from FLAC

} // namespace mdark
