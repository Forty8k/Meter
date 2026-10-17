// Meter F8K Analyser, command-line version: MeterF8KAnalyserCLI [--library <folder>] <files or folders...>
#include "Engine.h"
#include <iostream>

int main(int argc, char* argv[])
{
    juce::File library = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Meter F8K Library");
    juce::Array<juce::File> inputs;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if ((a == "--library" || a == "-l") && i + 1 < argc) {library = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); continue;}
        if (a == "--help" || a == "-h") {
            std::cout << "Usage: MeterF8KAnalyserCLI [--library <folder>] <audio files or folders...>\n"
                         "Adds each track to the library (default: Documents/Meter F8K Library). WAV, AIFF, FLAC, Ogg and MP3.\n";
            return 0;
        }
        f8k::Library::collectAudioFiles(juce::File::getCurrentWorkingDirectory().getChildFile(a), inputs);
    }
    if (inputs.isEmpty() && argc < 3) {std::cout << "No audio files given. Try --help\n"; return 1;}
    f8k::Library lib(library);
    std::cout << "Library: " << library.getFullPathName() << "\n";
    if (const int up = lib.upgradeOldRows()) std::cout << "Filled in newer values for " << up << " tracks already in the library\n";
    int added = 0, skipped = 0, failed = 0;
    for (auto& f : inputs) {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto r = lib.analyse(f);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        std::cout << (r.status == f8k::AnalyseOutcome::Added ? "added   " : r.status == f8k::AnalyseOutcome::Updated ? "updated " : r.status == f8k::AnalyseOutcome::AlreadyThere ? "skipped " : "FAILED  ")
                  << f.getFileName() << "  (" << r.message << ", " << juce::String(secs, 1) << " s)\n";
        if (r.status == f8k::AnalyseOutcome::Added || r.status == f8k::AnalyseOutcome::Updated) ++added; else if (r.status == f8k::AnalyseOutcome::AlreadyThere) ++skipped; else ++failed;
    }
    std::cout << added << " added, " << skipped << " already there, " << failed << " failed\n";
    return failed > 0 ? 2 : 0;
}
