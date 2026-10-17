// meterDARK Analyser: reads an audio file, captures it (Capture.h), replays the original scoring (OriginalScorer.h)
// and keeps the results in a library folder. Shared by the window app and the command-line tool.
//
// The library has two kinds of data, kept apart:
//  MEASURED  index.csv + tracks/*  written only by scanning. Never edited by hand.
//  YOURS     user.csv + columns.json  your tag fixes, BPM/key corrections and your own columns. Scanning never touches these.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <map>
#include "Capture.h"
#include "OriginalScorer.h"

namespace f8k {

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

class Library {
public:
    explicit Library(const juce::File& folder);
    juce::File folder;

    // scanning (any thread)
    AnalyseOutcome analyse(const juce::File& audio, std::function<void(double)> progress = {}, std::function<bool()> shouldStop = {});
    // Fills in what later versions added: grades from Meter versions a track has not been scored by yet (replayed from its
    // stored window data, or scanned again from its audio if that version measures differently), and BPM. Uses several cores.
    int upgradeOldRows(std::function<bool()> shouldStop = {}, std::function<void(int done, int total, const juce::String& file)> progress = {});
    int lastNoAudio = 0; // tracks whose audio was needed but could not be found (moved or deleted)
    static bool isAudioFile(const juce::File& f);
    static void collectAudioFiles(const juce::File& fileOrFolder, juce::Array<juce::File>& out);

    // reading and editing (any thread, all locked)
    Table index();                                      // a copy of index.csv
    Table grades(const juce::String& versionId);        // a copy of grades-<version>.csv: one Meter version's verdict on every track
    static std::vector<juce::String> versionIds();      // every Meter version the analyser has, oldest first
    std::map<juce::String, std::map<juce::String, juce::String>> userValues(); // id -> key -> value
    std::vector<CustomColumn> customColumns();
    void setUserValue(const juce::String& id, const juce::String& key, const juce::String& value);
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
    juce::File indexFile() const { return folder.getChildFile("index.csv"); }
    juce::File userFile() const { return folder.getChildFile("user.csv"); }
    juce::File columnsFile() const { return folder.getChildFile("columns.json"); }
    void writeReadme();
    void saveColumns(const std::vector<CustomColumn>& cols);
    void putIndexRow(const juce::StringArray& cells); // add, or replace the row with the same id
    void putGradeRow(const juce::String& vid, const juce::String& id, const OriginalResult& r);
    juce::File gradesFile(const juce::String& vid) const;
    juce::File coreFile(const juce::String& id, const juce::String& vid) const;
    bool scanAudio(const juce::File& audio, Capture* cap, std::vector<std::unique_ptr<MeterRun>>& runs, std::function<void(double)> progress,
                   std::function<bool()> shouldStop, juce::String& error, std::unique_ptr<juce::AudioFormatReader>* readerOut);
    bool scanVersions(const juce::String& id, const juce::File& audio, const std::vector<juce::String>& vids, std::function<void(double)> progress, std::function<bool()> shouldStop);
public:
    bool readCore(const juce::String& id, const juce::String& vid, std::vector<std::string>& names, std::vector<float>& values);
private:
};

juce::StringPairArray readFlacTags(const juce::File& f); // Vorbis comments (TITLE, ARTIST, DATE, GENRE ...), JUCE does not read these from FLAC

} // namespace f8k
