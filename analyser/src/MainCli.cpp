// meterDARK Analyser, command-line version: MeterF8KAnalyserCLI [--library <folder>] <files or folders...>
#include "Engine.h"
#include <iostream>

int main(int argc, char* argv[])
{
    juce::File library = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Meter F8K Library");
    if (!library.isDirectory()) library = library.getSiblingFile("meterDARK Library"); //an existing library keeps its folder
    juce::Array<juce::File> inputs;
    bool rescan = false; int levels = 0; int lean = 0; juce::String exportPack, importPack; bool fullPack = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if ((a == "--library" || a == "-l") && i + 1 < argc) {library = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); continue;}
        if (a == "--rescan") {rescan = true; continue;}
        if (a == "--levels") {levels = 1; continue;}
        if (a == "--lean") {lean = 1; continue;}
        if (a == "--export-pack" && i + 1 < argc) {exportPack = argv[++i]; continue;}
        if (a == "--full") {fullPack = true; continue;}
        if (a == "--import-pack" && i + 1 < argc) {importPack = argv[++i]; continue;}
        if (a == "--lean-check") {lean = 2; continue;}
        if (a == "--help" || a == "-h") {
            std::cout << "Usage: MeterF8KAnalyserCLI [--library <folder>] [--rescan] <audio files or folders...>\n"
                         "--rescan scans every track captured by an older analyser again from its stored file path.\n"
                         "--lean builds the small shareable record of every track (lean folder) from the stored window data; --lean-check also rebuilds them all and proves each replay block regrades to the same grade.\n"
                         "--export-pack <file> [--full] writes a share pack of every lean record (display only, or with the replay blocks); --import-pack <file> adds a pack to lean-shared.\n"
                         "--levels grades every track in the normalised and the mix level mode (replayed from the stored windows) and prints how many grades came out.\n"
                         "Adds each track to the library (default: Documents/meterDARK Library (or the older Meter F8K Library if you have one)). WAV, AIFF, FLAC, Ogg and MP3.\n";
            return 0;
        }
        f8k::Library::collectAudioFiles(juce::File::getCurrentWorkingDirectory().getChildFile(a), inputs);
    }
    if (inputs.isEmpty() && argc < 3 && !rescan && !levels && !lean && exportPack.isEmpty() && importPack.isEmpty()) {std::cout << "No audio files given. Try --help\n"; return 1;}
    f8k::Library lib(library);
    std::cout << "Library: " << library.getFullPathName() << "\n";
    if (const int up = lib.upgradeOldRows()) std::cout << "Filled in newer values for " << up << " tracks already in the library\n";
    if (rescan) {
        const int want = lib.tracksNeedingRescan();
        const int n = lib.rescanLibrary({}, [](int d, int t, const juce::String& f) { std::cout << "rescanned " << d << "/" << t << "  " << f << "\n"; }, [](const juce::String& m) { std::cout << m << "\n"; });
        std::cout << "Rescan: " << n << " of " << want << " tracks scanned again, " << lib.lastNoAudio << " left as they were\n";
    }
    if (lean) {
        const auto rep = lib.buildLeanRecords(lean == 2, lean == 2, {}, [](int d, int t) { if (d % 100 == 0 || d == t) std::cout << "lean records " << d << "/" << t << "\n"; });
        std::cout << "Lean records: " << rep.built << " built, " << rep.kept << " already there, " << rep.noData << " without window data";
        if (rep.built > 0) std::cout << ". Average " << juce::String(rep.bytesAll/rep.built/1024.0, 1) << " KB each (replay block " << juce::String(rep.bytesReplay/rep.built/1024.0, 1) << " KB), total " << juce::String(rep.bytesAll/1048576.0, 1) << " MB";
        if (lean == 2) std::cout << ". Replay mismatches: " << rep.mismatches;
        std::cout << "\n";
        if (lean == 2 && rep.mismatches > 0) return 3;
    }
    if (exportPack.isNotEmpty()) {
        std::vector<juce::String> ids; const auto t = lib.index();
        for (size_t r = 0; r < t.rows.size(); ++r) ids.push_back(t.get(r, "id"));
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(exportPack);
        const int n = lib.exportPack(out, fullPack, ids);
        std::cout << "Pack: " << n << " records written to " << out.getFullPathName() << " (" << (fullPack ? "full" : "display") << ", " << juce::String((double)out.getSize()/1024.0, 1) << " KB)\n";
    }
    if (importPack.isNotEmpty()) std::cout << "Pack import: " << lib.importPack(juce::File::getCurrentWorkingDirectory().getChildFile(importPack)) << "\n";
    if (levels) {
        const auto vid = f8k::Library::versionIds().back();
        for (int m = 1; m <= 2; ++m) {
            f8k::LevelMode lm; lm.mode = m;
            const auto base = lib.grades(vid);
            const auto t = lib.levelGrades(vid, 0, lm, true);
            int changed = 0;
            for (size_t r = 0; r < t.rows.size(); ++r) {const int b = base.findRow("id", t.get(r, "id")); if (b >= 0 && base.get((size_t)b, "grade") + lm.suffix() != t.get(r, "grade")) ++changed;}
            std::cout << "Levels: " << (m == 1 ? "normalised" : "mix") << " " << t.rows.size() << " of " << base.rows.size() << " tracks graded, " << changed << " differ from the original grade";
            for (size_t r = 0; r < t.rows.size() && r < 3; ++r) std::cout << "  [" << t.get(r, "grade") << "]";
            std::cout << "\n";
        }
    }
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
