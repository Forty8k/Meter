// meterDARK Analyser, command-line version: meterDARKAnalyserCLI [--library <folder>] <files or folders...>
#include "Engine.h"
#include "TagFill.h"
#include "TrackStore.h"
#include <iostream>

int main(int argc, char* argv[])
{
    juce::File library = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("meterDARK Library");
    juce::Array<juce::File> inputs;
    bool rescan = false; bool fillTags = false, foldersOnly = false, tagTest = false, storeTest = false; juce::String discogsToken; int levels = 0; int lean = 0; juce::String exportPack, importPack; bool fullPack = false, packNoNames = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if ((a == "--library" || a == "-l") && i + 1 < argc) {library = juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]); continue;}
        if (a == "--rescan") {rescan = true; continue;}
        if (a == "--levels") {levels = 1; continue;}
        if (a == "--lean") {lean = 1; continue;}
        if (a == "--export-pack" && i + 1 < argc) {exportPack = argv[++i]; continue;}
        if (a == "--full") {fullPack = true; continue;}
        if (a == "--no-names") {packNoNames = true; continue;}
        if (a == "--import-pack" && i + 1 < argc) {importPack = argv[++i]; continue;}
        if (a == "--lean-check") {lean = 2; continue;}
        if (a == "--fill-tags") {fillTags = true; continue;}
        if (a == "--folders-only") {foldersOnly = true; continue;}
        if (a == "--discogs-token" && i + 1 < argc) {discogsToken = argv[++i]; continue;}
        if (a == "--tagfill-test") {tagTest = true; continue;}
        if (a == "--store-test") {storeTest = true; continue;}
        if (a == "--help" || a == "-h") {
            std::cout << "Usage: meterDARKAnalyserCLI [--library <folder>] [--rescan] <audio files or folders...>\n"
                         "--rescan scans every track captured by an older analyser again from its stored file path.\n"
                         "--lean builds the small shareable record of every track (lean folder) from the stored window data; --lean-check also rebuilds them all and proves each replay block regrades to the same grade.\n"
                         "--export-pack <file> [--full] [--no-names] writes a share pack of every lean record (display only, or with the replay blocks; with track names unless --no-names); --import-pack <file> adds a pack to lean-shared.\n"
                         "--fill-tags [--folders-only] [--discogs-token <token>] fills EMPTY artist / album / year / genre from the folder names, then from MusicBrainz (and Discogs with a token). Your own edits only; tagfill-log.csv in the library lists every fill.\n"
                         "--levels grades every track in the normalised and the mix level mode (replayed from the stored windows) and prints how many grades came out.\n"
                         "Adds each track to the library (default: Documents/meterDARK Library). WAV, AIFF, FLAC, Ogg and MP3.\n";
            return 0;
        }
        mdark::Library::collectAudioFiles(juce::File::getCurrentWorkingDirectory().getChildFile(a), inputs);
    }
    if (tagTest) { //checks of the tag-filling rules and parsers on made-up paths and saved sample answers (no network)
        using namespace mdark::tagfill; int bad = 0;
        auto check = [&](bool ok, const char* what) { if (!ok) { ++bad; std::cout << "FAIL: " << what << "\n"; } };
        { const auto f = fromPath("D:/Music/Rock/Foo Fighters/The Colour and the Shape (1997)/01 Monkey Wrench.wav"); check(f.genre == "Rock" && f.artist == "Foo Fighters" && f.album == "The Colour and the Shape" && f.year == "1997", "Rock/Artist/Album (1997)"); }
        { const auto f = fromPath("/home/x/Hip-Hop/Run DMC/It's Tricky.flac"); check(f.genre == "Hip Hop" && f.artist == "Run DMC" && f.album.isEmpty(), "Genre/Artist/track"); }
        { const auto f = fromPath("C:\\Music\\Jazz\\Miles Davis\\1959 - Kind of Blue\\So What.wav"); check(f.genre == "Jazz" && f.artist == "Miles Davis" && f.album == "Kind of Blue" && f.year == "1959", "windows path, year first"); }
        { const auto f = fromPath("/music/loose/track.wav"); check(f.genre.isEmpty() && f.year.isEmpty(), "no genre or year invented"); }
        check(genreFromFolderName("Drum & Bass") == "Drum & Bass" && genreFromFolderName("R'n'B").isEmpty() == false && genreFromFolderName("Holiday Mix").isEmpty(), "genre names");
        check(cleanTitle("Wired for Sound (2001 Remaster)") == "Wired for Sound" && cleanTitle("Rehab - 2015 Remaster") == "Rehab" && cleanTitle("Song (feat. X)") == "Song" && cleanTitle("Heroes") == "Heroes", "cleanTitle");
        { const auto m = parseMusicBrainz(R"({"recordings":[{"score":100,"first-release-date":"1997-05-20","releases":[{"date":"1998-01-01"},{"date":"1997-05-20"}],"tags":[{"count":3,"name":"rock"},{"count":9,"name":"alternative rock"}]},{"score":50,"first-release-date":"1960-01-01"}]})");
          check(m.ok && m.year == "1997" && m.genre == "Alternative rock", "musicbrainz parse"); }
        { const auto d = parseDiscogs(R"({"results":[{"year":"2003","genre":["Rock"]},{"year":"1999","genre":["Electronic"]}]})"); check(d.ok && d.year == "1999" && d.genre == "Rock", "discogs parse"); }
        check(!parseMusicBrainz("not json").ok && !parseDiscogs("{}").ok, "bad answers");
        std::cout << (bad == 0 ? "Tag-fill checks: all passed\n" : "Tag-fill checks: FAILED\n");
        return bad == 0 ? 0 : 1;
    }
    if (storeTest) { //the track store: filled once, then changed one track at a time (no disk re-reading), in a temporary library
        int bad = 0;
        auto check = [&](bool ok, const char* what) { if (!ok) { ++bad; std::cout << "FAIL: " << what << "\n"; } };
        const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mdark-store-test-" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        auto makeWav = [&](const juce::String& name, double hz) {
            const juce::File f = tmp.getChildFile(name);
            juce::WavAudioFormat wav; std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), 48000.0, 2, 16, {}, 0));
            if (w == nullptr) return f;
            os.release();
            juce::AudioBuffer<float> b(2, 48000 * 3);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.4f*(float)std::sin(6.2831853*hz*i/48000.0); b.setSample(0, i, v); b.setSample(1, i, v); }
            w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            return f;
        };
        auto lib = std::make_shared<mdark::Library>(tmp.getChildFile("lib"));
        mdark::TrackStore store(lib);                       // created BEFORE anything is written, as the header says
        const auto vids = mdark::Library::versionIds();
        const mdark::TrackStore::WindowKey key{vids.back(), 96000, false};
        const juce::File a = makeWav("a.wav", 110.0), b = makeWav("b.wav", 440.0), c = makeWav("c.wav", 1760.0);
        check(store.trackCount() == 0, "empty library");
        uint64_t s0 = store.seq();
        const auto ra = lib->analyse(a), rb = lib->analyse(b);
        check(ra.status == mdark::AnalyseOutcome::Added && rb.status == mdark::AnalyseOutcome::Added, "two tracks added");
        check(store.trackCount() == 2 && lib->index().rows.size() == 2, "store has the two tracks without a reload");
        { std::vector<mdark::StoreChange> ch; uint64_t now = 0; const bool ok = store.changesSince(s0, ch, now);
          int added = 0; for (auto& x : ch) if (x.kind == mdark::StoreChange::Added) ++added;
          check(ok && added == 2 && now == store.seq(), "the log says two tracks were added"); }
        for (auto& v : vids) check(store.readings(v).rows.size() == 2 && store.nativeGrades(v).rows.size() == 2, "grades of every version are in the store");
        // what the store holds is what the files hold
        { const auto fromDisk = lib->index(); const auto mem = store.index(); check(fromDisk.header == mem.header && fromDisk.rows.size() == mem.rows.size(), "store index == index.csv");
          for (size_t r = 0; r < fromDisk.rows.size(); ++r) if (fromDisk.rows[r] != mem.rows[(size_t)mem.findRow("id", fromDisk.get(r, "id"))]) check(false, "an index row differs"); }
        { const auto d = lib->grades(vids.back(), 96000), m = store.readings(vids.back()); check(d.rows.size() == m.rows.size(), "readings == grades at 96 kHz"); }
        // window data: read once, kept
        const std::vector<juce::String> ids{ra.id, rb.id};
        check(store.loadWindows(key, ids) == 2 && store.windowsKept(key) == 2, "window data read for the two tracks");
        check(store.loadWindows(key, ids) == 0, "second ask reads nothing");
        const auto w1 = store.windows(key, ra.id);
        check(w1 != nullptr && !w1->values.empty() && !w1->names.empty() && w1->sr > 1000.0, "window data usable");
        // your values change: only values, no window data touched
        s0 = store.seq();
        lib->setUserValue(ra.id, "genre", "Test genre");
        check(store.userValuesOf(ra.id)["genre"] == "Test genre", "your value is in the store at once");
        { std::vector<mdark::StoreChange> ch; uint64_t now = 0; store.changesSince(s0, ch, now); check(ch.size() == 1 && ch[0].kind == mdark::StoreChange::Values && ch[0].id == ra.id, "one Values change for one track"); }
        check(store.windows(key, ra.id) == w1 && store.windowsKept(key) == 2, "window data untouched by a value change");
        lib->setUserValue(ra.id, "genre", "");
        check(store.userValuesOf(ra.id).count("genre") == 0, "an emptied value is gone");
        // a third track tags on the end; the other two keep their data
        s0 = store.seq();
        const auto rc = lib->analyse(c);
        check(store.trackCount() == 3, "third track appended");
        check(store.windows(key, ra.id) == w1, "the first track's window data is the very same object (nothing rebuilt)");
        check(store.loadWindows(key, {ra.id, rb.id, rc.id}) == 1 && store.windowsKept(key) == 3, "only the new track's window data was read");
        { std::vector<mdark::StoreChange> ch; uint64_t now = 0; store.changesSince(s0, ch, now); int added = 0; for (auto& x : ch) if (x.kind == mdark::StoreChange::Added && x.id == rc.id) ++added; check(added == 1, "one Added change for the new track"); }
        // adding the same file again changes nothing
        s0 = store.seq();
        check(lib->analyse(c).status == mdark::AnalyseOutcome::AlreadyThere && store.seq() == s0, "an already-known file changes nothing");
        // lean records are read once and kept
        lib->buildLeanRecords(false, false);
        const auto l1 = store.lean(ra.id);
        check(l1 != nullptr && store.lean(ra.id) == l1, "lean record read once, then kept");
        // removing a track
        s0 = store.seq();
        lib->removeTrack(rb.id);
        check(store.trackCount() == 2 && store.readings(vids.back()).rows.size() == 2 && store.windowsKept(key) == 2, "removed track gone everywhere");
        { std::vector<mdark::StoreChange> ch; uint64_t now = 0; store.changesSince(s0, ch, now); int rem = 0; for (auto& x : ch) if (x.kind == mdark::StoreChange::Removed && x.id == rb.id) ++rem; check(rem == 1, "one Removed change"); }
        // too far behind the log
        { std::vector<mdark::StoreChange> ch; uint64_t now = 0; check(!store.changesSince(store.seq() + 5, ch, now), "a number from the future is refused"); }
        // share packs: names travel (no paths), a second import updates, and a store that already looked at lean-shared sees the new records
        {
            lib->setUserValue(rc.id, "title", "Cee Song"); lib->setUserValue(rc.id, "artist", "Tester");
            const juce::File named = tmp.getChildFile("named.mdarkpack"), anon = tmp.getChildFile("anon.mdarkpack");
            check(lib->exportPack(named, true, {ra.id, rc.id}, true) == 2 && lib->exportPack(anon, true, {ra.id, rc.id}, false) == 2, "two packs written");
            auto lib2 = std::make_shared<mdark::Library>(tmp.getChildFile("lib2"));
            mdark::TrackStore store2(lib2);
            check(store2.sharedIds().empty(), "nothing shared yet (and the store has looked)");
            std::vector<juce::String> fresh; int upd = -1;
            lib2->importPack(named, nullptr, &fresh, &upd);
            check(fresh.size() == 2 && upd == 0, "2 new, 0 updated");
            check(store2.sharedIds().size() == 2, "the store sees the imported records at once");
            const auto sa = store2.shared(ra.id), sc = store2.shared(rc.id);
            check(sa != nullptr && sa->get("name") == "a", "a name from the file name, without folder or extension");
            check(sc != nullptr && sc->get("name") == "Tester - Cee Song" && sc->get("title") == "Cee Song", "your values win for the name and tags");
            check(sa != nullptr && sa->get("source_path").empty() && sa->get("file").empty(), "no paths in a shared record");
            fresh.clear(); lib2->importPack(named, nullptr, &fresh, &upd);
            check(fresh.empty() && upd == 2, "importing again: 0 new, 2 updated");
            lib2->importPack(anon, nullptr, nullptr, &upd);
            const auto sa2 = store2.shared(ra.id);
            check(sa2 != nullptr && sa2->get("name").empty(), "an anonymous pack carries no names (and the store shows the new copy)");
            check(store2.windows({vids.back(), 0, true}, ra.id) != nullptr, "a full pack's replay block can be replayed");
        }
        tmp.deleteRecursively();
        std::cout << (bad == 0 ? "Track store checks: all passed\n" : "Track store checks: FAILED\n");
        return bad == 0 ? 0 : 1;
    }
    if (fillTags) {
        mdark::Library flib(library);
        mdark::tagfill::Options o; o.musicBrainz = !foldersOnly; o.discogs = !foldersOnly && discogsToken.isNotEmpty(); o.discogsToken = discogsToken;
        const auto sum = mdark::tagfill::fill(flib, o, [](int d, int t, const juce::String& m) { if (d % 50 == 0) std::cout << d << "/" << t << "  " << m << "\n"; }, {});
        std::cout << "Tag fill: " << sum.genres << " genres, " << sum.years << " years, " << sum.artists << " artists, " << sum.albums << " albums filled (" << sum.lookups << " lookups, " << sum.failures << " failed). Nothing was overwritten.\n";
        return 0;
    }
    if (inputs.isEmpty() && argc < 3 && !rescan && !levels && !lean && exportPack.isEmpty() && importPack.isEmpty()) {std::cout << "No audio files given. Try --help\n"; return 1;}
    mdark::Library lib(library);
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
        const int n = lib.exportPack(out, fullPack, ids, !packNoNames);
        std::cout << "Pack: " << n << " records written to " << out.getFullPathName() << " (" << (fullPack ? "full" : "display") << (packNoNames ? ", no names" : ", with names") << ", " << juce::String((double)out.getSize()/1024.0, 1) << " KB)\n";
    }
    if (importPack.isNotEmpty()) std::cout << "Pack import: " << lib.importPack(juce::File::getCurrentWorkingDirectory().getChildFile(importPack)) << "\n";
    if (levels) {
        const auto vid = mdark::Library::versionIds().back();
        for (int m = 1; m <= 2; ++m) {
            mdark::LevelMode lm; lm.mode = m;
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
        std::cout << (r.status == mdark::AnalyseOutcome::Added ? "added   " : r.status == mdark::AnalyseOutcome::Updated ? "updated " : r.status == mdark::AnalyseOutcome::AlreadyThere ? "skipped " : "FAILED  ")
                  << f.getFileName() << "  (" << r.message << ", " << juce::String(secs, 1) << " s)\n";
        if (r.status == mdark::AnalyseOutcome::Added || r.status == mdark::AnalyseOutcome::Updated) ++added; else if (r.status == mdark::AnalyseOutcome::AlreadyThere) ++skipped; else ++failed;
    }
    std::cout << added << " added, " << skipped << " already there, " << failed << " failed\n";
    return failed > 0 ? 2 : 0;
}
