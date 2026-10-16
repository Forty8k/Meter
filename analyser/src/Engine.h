// Meter F8K Analyser: reads an audio file, captures it (Capture.h), replays the original scoring (OriginalScorer.h)
// and writes the results into a library folder. Shared by the window app and the command-line tool.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace f8k {

struct AnalyseOutcome {
    enum Status { Added, AlreadyThere, Failed } status = Failed;
    juce::String id, message;
};

class Library {
public:
    explicit Library(const juce::File& folder);
    // Analyse one audio file into the library. progress (0-1) may be called from this thread.
    AnalyseOutcome analyse(const juce::File& audio, std::function<void(double)> progress = {}, std::function<bool()> shouldStop = {});
    static bool isAudioFile(const juce::File& f);
    static void collectAudioFiles(const juce::File& fileOrFolder, juce::Array<juce::File>& out);
    juce::File folder;
private:
    juce::AudioFormatManager formats;
    juce::CriticalSection indexLock;
    bool idInIndex(const juce::String& id);
    void appendIndexRow(const juce::StringArray& cells);
    void writeReadme();
};

juce::StringPairArray readFlacTags(const juce::File& f); // Vorbis comments (TITLE, ARTIST, DATE, GENRE ...), JUCE does not read these from FLAC

} // namespace f8k
