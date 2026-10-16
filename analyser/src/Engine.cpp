#include "Engine.h"
#include "Capture.h"
#include "OriginalScorer.h"
#include <juce_cryptography/juce_cryptography.h>

namespace f8k {

static const char* kIndexHeader[] = {"id", "file", "artist", "title", "album", "year", "genre", "format", "lossy", "sample_rate", "bits", "channels",
    "duration_s", "peak_dbfs", "true_peak_dbtp", "lufs_integrated", "original_grade", "letter", "bonus", "authority", "power", "detail", "directions",
    "authority_value", "power_value", "detail_value", "core_windows", "analysed", "capture_version", "source_path"};

static juce::String csvCell(const juce::String& s)
{
    if (s.containsAnyOf(",\"\n\r")) return "\"" + s.replace("\"", "\"\"") + "\"";
    return s;
}

static juce::String num(double v, int dp)
{
    if (!std::isfinite(v)) return "";
    return juce::String(v, dp);
}

Library::Library(const juce::File& f) : folder(f)
{
    formats.registerBasicFormats(); //WAV, AIFF, FLAC, Ogg (and MP3 when built in)
    folder.createDirectory();
    folder.getChildFile("tracks").createDirectory();
    writeReadme();
}

bool Library::isAudioFile(const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension("wav;wave;flac;aif;aiff;ogg;mp3;bwf");
}

void Library::collectAudioFiles(const juce::File& f, juce::Array<juce::File>& out)
{
    if (f.isDirectory()) {
        for (const auto& entry : juce::RangedDirectoryIterator(f, true, "*", juce::File::findFiles))
            if (isAudioFile(entry.getFile())) out.add(entry.getFile());
    } else if (isAudioFile(f)) out.add(f);
}

juce::StringPairArray readFlacTags(const juce::File& f)
{
    juce::StringPairArray tags;
    juce::FileInputStream in(f);
    if (!in.openedOk()) return tags;
    char magic[4];
    if (in.read(magic, 4) != 4 || memcmp(magic, "fLaC", 4) != 0) return tags;
    for (int guard = 0; guard < 64 && !in.isExhausted(); ++guard) {
        const int head = in.readByte() & 0xff;
        const int len = ((in.readByte() & 0xff) << 16) | ((in.readByte() & 0xff) << 8) | (in.readByte() & 0xff);
        const bool last = (head & 0x80) != 0;
        if ((head & 0x7f) == 4) { //VORBIS_COMMENT
            juce::MemoryBlock mb; in.readIntoMemoryBlock(mb, len);
            juce::MemoryInputStream m(mb, false);
            const int vendorLen = m.readInt(); m.skipNextBytes(vendorLen);
            const int count = m.readInt();
            for (int i = 0; i < count && !m.isExhausted(); ++i) {
                const int l = m.readInt();
                if (l < 0 || l > 1 << 20) break;
                juce::MemoryBlock e; m.readIntoMemoryBlock(e, l);
                const juce::String kv = juce::String::fromUTF8((const char*)e.getData(), (int)e.getSize());
                const juce::String key = kv.upToFirstOccurrenceOf("=", false, false).toUpperCase();
                if (key.isNotEmpty() && !tags.containsKey(key)) tags.set(key, kv.fromFirstOccurrenceOf("=", false, false));
            }
            break;
        }
        in.skipNextBytes(len);
        if (last) break;
    }
    return tags;
}

static juce::String pickTag(const juce::StringPairArray& t, std::initializer_list<const char*> keys)
{
    for (auto k : keys) {
        for (auto& key : t.getAllKeys()) if (key.equalsIgnoreCase(k) && t[key].trim().isNotEmpty()) return t[key].trim();
    }
    return {};
}

bool Library::idInIndex(const juce::String& id)
{
    const juce::ScopedLock sl(indexLock);
    juce::StringArray lines; lines.addLines(folder.getChildFile("index.csv").loadFileAsString());
    for (auto& l : lines) if (l.startsWith(id + ",")) return true;
    return false;
}

void Library::appendIndexRow(const juce::StringArray& cells)
{
    const juce::ScopedLock sl(indexLock);
    const juce::File index = folder.getChildFile("index.csv");
    juce::String text;
    if (!index.existsAsFile()) {
        juce::StringArray h; for (auto c : kIndexHeader) h.add(c);
        text << h.joinIntoString(",") << "\r\n";
    }
    juce::StringArray q; for (auto& c : cells) q.add(csvCell(c));
    text << q.joinIntoString(",") << "\r\n";
    index.appendText(text, false, false, nullptr);
}

AnalyseOutcome Library::analyse(const juce::File& audio, std::function<void(double)> progress, std::function<bool()> shouldStop)
{
    AnalyseOutcome out;
    const juce::String id = juce::SHA256(audio).toHexString().substring(0, 16); //the same file content always gets the same id
    out.id = id;
    if (idInIndex(id)) {out.status = AnalyseOutcome::AlreadyThere; out.message = "already in the library"; return out;}

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audio));
    if (reader == nullptr) {out.message = "could not open this file as audio"; return out;}
    const double sr = reader->sampleRate;
    const int numCh = (int)reader->numChannels;
    const juce::int64 length = reader->lengthInSamples;
    if (sr < 8000.0 || numCh < 1 || length < 1) {out.message = "no usable audio in this file"; return out;}

    Capture cap(sr);
    const int chunk = 65536;
    juce::AudioBuffer<float> buf(juce::jmax(2, numCh), chunk);
    for (juce::int64 pos = 0; pos < length; pos += chunk) {
        if (shouldStop && shouldStop()) {out.message = "stopped"; return out;}
        const int n = (int)juce::jmin<juce::int64>(chunk, length - pos);
        buf.clear();
        reader->read(&buf, 0, n, pos, true, numCh > 1);
        const float* L = buf.getReadPointer(0);
        const float* R = numCh > 1 ? buf.getReadPointer(1) : L; //mono plays as dual mono, as on a stereo track
        cap.process(L, R, n);
        if (progress) progress((double)(pos + n) / (double)length * 0.9);
    }
    cap.finish();

    //the original meter's verdict, at its default size and in real time
    OriginalReplay replay;
    for (auto& w : cap.core) replay.push(w);
    const OriginalResult orig = replay.result();

    //tags
    juce::StringPairArray tags = reader->metadataValues;
    if (audio.hasFileExtension("flac")) tags.addArray(readFlacTags(audio));
    const juce::String artist = pickTag(tags, {"ARTIST", "IART", "ALBUMARTIST"});
    const juce::String title = pickTag(tags, {"TITLE", "INAM"});
    const juce::String album = pickTag(tags, {"ALBUM", "IPRD"});
    const juce::String year = pickTag(tags, {"DATE", "YEAR", "ICRD", "ORIGINALDATE"}).substring(0, 4);
    const juce::String genre = pickTag(tags, {"GENRE", "IGNR"});
    const bool lossy = audio.hasFileExtension("mp3;ogg");

    const juce::File tracks = folder.getChildFile("tracks");
    //CORE: one row per window, the ten values exactly as the plugin sends them
    {
        juce::String csv;
        csv.preallocateBytes((size_t)cap.core.size() * 140);
        csv << "window,time_s,peakL,peakR,slewL,slewR,zeroL,zeroR,beamL,beamR,deptL,deptR\r\n";
        char line[400];
        for (size_t i = 0; i < cap.core.size(); ++i) {
            const auto& w = cap.core[i];
            snprintf(line, sizeof line, "%zu,%.6f,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\r\n", i, (double)(i+1)*cap.coreWindowSamples/sr,
                     w.peakL, w.peakR, w.slewL, w.slewR, w.zeroL, w.zeroR, w.beamL, w.beamR, w.deptL, w.deptR);
            csv << line;
        }
        tracks.getChildFile(id + ".core.csv").replaceWithText(csv, false, false, "\r\n");
    }
    if (progress) progress(0.95);
    //FINE: little-endian float32 rows, gzip compressed
    {
        const juce::File fineFile = tracks.getChildFile(id + ".fine.f32.gz");
        fineFile.deleteFile();
        juce::FileOutputStream fos(fineFile);
        if (!fos.openedOk()) {out.message = "could not write to the library folder"; return out;}
        juce::GZIPCompressorOutputStream gz(fos, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        for (float v : cap.fine) gz.writeFloat(v); //JUCE writes little-endian
        gz.flush();
    }
    //META: everything about the track and how it was captured
    {
        auto* o = new juce::DynamicObject();
        juce::var meta(o);
        o->setProperty("id", id);
        o->setProperty("file", audio.getFileName());
        o->setProperty("source_path", audio.getFullPathName());
        o->setProperty("file_size", (juce::int64)audio.getSize());
        o->setProperty("format", reader->getFormatName());
        o->setProperty("lossy", lossy);
        o->setProperty("sample_rate", sr);
        o->setProperty("bits", (int)reader->bitsPerSample);
        o->setProperty("channels", numCh);
        o->setProperty("length_samples", (juce::int64)length);
        o->setProperty("duration_s", (double)length / sr);
        auto* t = new juce::DynamicObject();
        for (auto& k : tags.getAllKeys()) t->setProperty(k, tags[k]);
        o->setProperty("tags", juce::var(t));
        o->setProperty("capture_version", juce::String(kCaptureVersion));
        o->setProperty("original_meter_version", "Airwindows Meter 0.5.5");
        o->setProperty("core_window_samples", cap.coreWindowSamples);
        o->setProperty("core_windows", (int)cap.core.size());
        o->setProperty("fine_block_samples", cap.fineBlockSamples);
        o->setProperty("fine_blocks", (int)cap.fineRows());
        juce::Array<juce::var> cols; for (auto& c : fineColumnNames()) cols.add(juce::String(c));
        o->setProperty("fine_columns", cols);
        juce::Array<juce::var> bands; for (double b : kBandCentres) bands.add(b);
        o->setProperty("band_centres_hz", bands);
        o->setProperty("peak_dbfs", 20.0*std::log10(std::max(cap.peakAbs, 1e-12)));
        o->setProperty("true_peak_dbtp", 20.0*std::log10(std::max(cap.truePeakAbs, 1e-12)));
        const double lufs = cap.integratedLufs();
        if (std::isfinite(lufs)) o->setProperty("lufs_integrated", lufs);
        auto* g = new juce::DynamicObject();
        g->setProperty("grade", juce::String(orig.grade)); g->setProperty("letter", juce::String(orig.letter)); g->setProperty("bonus", juce::String(orig.bonus));
        g->setProperty("authority", juce::String(orig.authority)); g->setProperty("power", juce::String(orig.power)); g->setProperty("detail", juce::String(orig.detail));
        g->setProperty("directions", juce::String(orig.directions));
        g->setProperty("authority_value", orig.bassPrevGrade); g->setProperty("power_value", orig.peakPrevGrade); g->setProperty("detail_value", orig.slewPrevGrade);
        g->setProperty("score_slews", orig.outputR); g->setProperty("score_zero_crosses", orig.outputG); g->setProperty("score_peaks", orig.outputB);
        g->setProperty("display_width", 1058); g->setProperty("paints", (int)orig.paints);
        o->setProperty("original_meter", juce::var(g));
        o->setProperty("analysed", juce::Time::getCurrentTime().toISO8601(true));
        tracks.getChildFile(id + ".meta.json").replaceWithText(juce::JSON::toString(meta), false, false, "\n");

        appendIndexRow({id, audio.getFileName(), artist, title, album, year, genre, reader->getFormatName(), lossy ? "yes" : "no",
            num(sr, 0), juce::String((int)reader->bitsPerSample), juce::String(numCh), num((double)length/sr, 2),
            num(20.0*std::log10(std::max(cap.peakAbs, 1e-12)), 2), num(20.0*std::log10(std::max(cap.truePeakAbs, 1e-12)), 2), num(lufs, 2),
            juce::String(orig.grade), juce::String(orig.letter), juce::String(orig.bonus), juce::String(orig.authority), juce::String(orig.power),
            juce::String(orig.detail), juce::String(orig.directions), num(orig.bassPrevGrade, 3), num(orig.peakPrevGrade, 3), num(orig.slewPrevGrade, 3),
            juce::String((int)cap.core.size()), juce::Time::getCurrentTime().toISO8601(true), juce::String(kCaptureVersion), audio.getFullPathName()});
    }
    if (progress) progress(1.0);
    out.status = AnalyseOutcome::Added;
    out.message = orig.grade.empty() ? juce::String("added") : "added, original meter reads " + juce::String(orig.grade);
    return out;
}

void Library::writeReadme()
{
    const juce::File f = folder.getChildFile("README.txt");
    if (f.existsAsFile()) return;
    f.replaceWithText(
        "METER F8K LIBRARY\r\n"
        "=================\r\n"
        "Made by Meter F8K Analyser. Each track you add is captured once and kept here. Nothing is ever interpreted on the way in:\r\n"
        "every gauge, grade or hint can be worked out again later from these files, without the audio.\r\n\r\n"
        "index.csv              one row per track: tags, format, levels, loudness, and what the ORIGINAL Airwindows Meter 0.5.5 reads.\r\n"
        "tracks/<id>.core.csv   the original meter's own data: one row per audio window (1882 samples at 44.1 kHz, about 42.7 ms),\r\n"
        "                       the ten values the plugin sends to its display (peak is square-rooted, as in the plugin).\r\n"
        "tracks/<id>.fine.f32.gz  short blocks (256 samples at 44.1 kHz, about 5.8 ms), gzip-compressed little-endian float32 rows.\r\n"
        "                       The columns are listed in <id>.meta.json (fine_columns). Peaks, slews, zero-cross positions, beam and\r\n"
        "                       departure combine exactly into any longer window. Extras: RMS, DC, true peak, K-weighted loudness,\r\n"
        "                       stereo correlation, mid/side, ten octave bands of the mid signal.\r\n"
        "tracks/<id>.meta.json  everything about the file and how it was captured, including all its tags.\r\n\r\n"
        "The id is part of a SHA-256 of the file, so adding the same file twice is skipped. Delete a track's files and its index row to remove it.\r\n",
        false, false, "\r\n");
}

} // namespace f8k
