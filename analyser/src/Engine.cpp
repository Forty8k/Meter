#include "Engine.h"
#include "Capture.h"
#include "OriginalScorer.h"
#include "Estimates.h"
#include <juce_cryptography/juce_cryptography.h>

namespace f8k {

static const char* kIndexHeader[] = {"id", "file", "artist", "title", "album", "year", "genre", "format", "lossy", "sample_rate", "bits", "channels",
    "duration_s", "peak_dbfs", "true_peak_dbtp", "lufs_integrated", "original_grade", "letter", "bonus", "authority", "power", "detail", "directions",
    "authority_value", "power_value", "detail_value", "letter_value", "bonus_value", "bpm_est", "bpm_confidence", "key_est", "key_confidence", "core_windows", "analysed", "capture_version", "source_path"};

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

//---------------------------------------------------------------- Table
void Table::set(size_t row, const juce::String& name, const juce::String& v)
{
    int c = col(name);
    if (c < 0) {header.add(name); c = header.size() - 1;}
    while (rows[row].size() <= c) rows[row].add({});
    rows[row].set(c, v);
}
int Table::findRow(const juce::String& key, const juce::String& value) const
{
    const int c = col(key);
    if (c < 0) return -1;
    for (size_t r = 0; r < rows.size(); ++r) if (c < rows[r].size() && rows[r][c] == value) return (int)r;
    return -1;
}
std::vector<juce::StringArray> Table::parse(const juce::String& text)
{
    std::vector<juce::StringArray> out;
    juce::StringArray row; juce::String cell; bool quoted = false;
    auto p = text.getCharPointer();
    if (*p == 0xFEFF) ++p; //byte order mark
    for (;;) {
        const juce::juce_wchar ch = p.getAndAdvance();
        if (ch == 0) {if (cell.isNotEmpty() || row.size() > 0) {row.add(cell); out.push_back(row);} break;}
        if (quoted) {
            if (ch == '"') {if (*p == '"') {cell << '"'; ++p;} else quoted = false;}
            else cell << juce::String::charToString(ch);
        } else if (ch == '"') quoted = true;
        else if (ch == ',' || ch == '\t') {row.add(cell); cell = {};}
        else if (ch == '\r') {}
        else if (ch == '\n') {row.add(cell); cell = {}; out.push_back(row); row = {};}
        else cell << juce::String::charToString(ch);
    }
    while (!out.empty() && out.back().joinIntoString("").trim().isEmpty()) out.pop_back();
    return out;
}
bool Table::load(const juce::File& f)
{
    header.clear(); rows.clear();
    if (!f.existsAsFile()) return false;
    auto all = parse(f.loadFileAsString());
    if (all.empty()) return false;
    header = all[0];
    rows.assign(all.begin() + 1, all.end());
    return true;
}
bool Table::save(const juce::File& f) const
{
    juce::String text;
    juce::StringArray h; for (auto& c : header) h.add(csvCell(c));
    text << h.joinIntoString(",") << "\r\n";
    for (auto& r : rows) {juce::StringArray q; for (int i = 0; i < header.size(); ++i) q.add(csvCell(i < r.size() ? r[i] : juce::String())); text << q.joinIntoString(",") << "\r\n";}
    juce::TemporaryFile tmp(f);
    if (!tmp.getFile().replaceWithText(text, false, false, nullptr)) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

void Library::putIndexRow(const juce::StringArray& cells)
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(indexFile());
    juce::StringArray want; for (auto c : kIndexHeader) want.add(c);
    if (t.header != want) { //an older index: carry its rows over to the current columns
        Table n; n.header = want;
        for (size_t r = 0; r < t.rows.size(); ++r) {
            n.rows.push_back(juce::StringArray());
            for (auto& c : want) n.rows.back().add(t.get(r, c));
        }
        t = n;
    }
    const int existing = t.findRow("id", cells[0]);
    if (existing >= 0) t.rows[(size_t)existing] = cells; else t.rows.push_back(cells);
    t.save(indexFile());
    ++changes;
}

AnalyseOutcome Library::analyse(const juce::File& audio, std::function<void(double)> progress, std::function<bool()> shouldStop)
{
    AnalyseOutcome out;
    const juce::String id = juce::SHA256(audio).toHexString().substring(0, 16); //the same file content always gets the same id
    out.id = id;
    bool replacing = false;
    {
        Table t = index();
        const int r = t.findRow("id", id);
        if (r >= 0) {
            if (t.get((size_t)r, "capture_version").getDoubleValue() >= juce::String(kCaptureVersion).getDoubleValue()) {
                out.status = AnalyseOutcome::AlreadyThere; out.message = "already in the library"; return out;
            }
            replacing = true; //scanned by an older version: scan again and update it in place (your own values are kept)
        }
    }

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
    const BpmEstimate bpm = estimateBpm(cap.fine, cap.fineBlockSamples/sr);
    const KeyEstimate key = estimateKey(cap.pitch.frames);

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
    if (progress) progress(0.93);
    //PITCH: little-endian float32 rows of 13 (12 note strengths C..B, then RMS), gzip compressed
    {
        const juce::File pf = tracks.getChildFile(id + ".pitch.f32.gz");
        pf.deleteFile();
        juce::FileOutputStream fos(pf);
        if (fos.openedOk()) {
            juce::GZIPCompressorOutputStream gz(fos, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            for (float v : cap.pitch.frames) gz.writeFloat(v);
            gz.flush();
        }
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
        o->setProperty("pitch_fft_size", cap.pitch.fftSize);
        o->setProperty("pitch_hop", cap.pitch.hop);
        o->setProperty("pitch_frames", (int)cap.pitch.numFrames());
        o->setProperty("pitch_columns", juce::Array<juce::var>({"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B", "rms"}));
        o->setProperty("bpm_est", bpm.bpm); o->setProperty("bpm_confidence", bpm.confidence);
        o->setProperty("key_est", juce::String(key.name)); o->setProperty("key_confidence", key.confidence);
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
        g->setProperty("letter_value", orig.letterValue); g->setProperty("bonus_value", orig.bonusValue);
        g->setProperty("display_width", 1058); g->setProperty("paints", (int)orig.paints);
        o->setProperty("original_meter", juce::var(g));
        o->setProperty("analysed", juce::Time::getCurrentTime().toISO8601(true));
        tracks.getChildFile(id + ".meta.json").replaceWithText(juce::JSON::toString(meta), false, false, "\n");

        putIndexRow({id, audio.getFileName(), artist, title, album, year, genre, reader->getFormatName(), lossy ? "yes" : "no",
            num(sr, 0), juce::String((int)reader->bitsPerSample), juce::String(numCh), num((double)length/sr, 2),
            num(20.0*std::log10(std::max(cap.peakAbs, 1e-12)), 2), num(20.0*std::log10(std::max(cap.truePeakAbs, 1e-12)), 2), num(lufs, 2),
            juce::String(orig.grade), juce::String(orig.letter), juce::String(orig.bonus), juce::String(orig.authority), juce::String(orig.power),
            juce::String(orig.detail), juce::String(orig.directions), num(orig.bassPrevGrade, 3), num(orig.peakPrevGrade, 3), num(orig.slewPrevGrade, 3),
            num(orig.letterValue, 3), juce::String(orig.bonusValue), bpm.bpm > 0 ? num(bpm.bpm, 1) : juce::String(), num(bpm.confidence, 2),
            juce::String(key.name), num(key.confidence, 3),
            juce::String((int)cap.core.size()), juce::Time::getCurrentTime().toISO8601(true), juce::String(kCaptureVersion), audio.getFullPathName()});
    }
    if (progress) progress(1.0);
    out.status = replacing ? AnalyseOutcome::Updated : AnalyseOutcome::Added;
    out.message = juce::String(replacing ? "updated" : "added") + (orig.grade.empty() ? juce::String() : ", original meter reads " + juce::String(orig.grade)) + (bpm.bpm > 0 ? ", ~" + juce::String(juce::roundToInt(bpm.bpm)) + " BPM" : juce::String()) + (key.name.empty() ? juce::String() : ", " + juce::String(key.name));
    return out;
}

//---------------------------------------------------------------- reading and editing
Table Library::index()
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(indexFile());
    return t;
}

const juce::StringArray& Library::editableBuiltIns()
{
    static const juce::StringArray k{"artist", "title", "album", "year", "genre", "bpm", "key"};
    return k;
}

std::map<juce::String, std::map<juce::String, juce::String>> Library::userValues()
{
    const juce::ScopedLock sl(lock);
    std::map<juce::String, std::map<juce::String, juce::String>> out;
    Table t; t.load(userFile());
    for (size_t r = 0; r < t.rows.size(); ++r) {
        const juce::String id = t.get(r, "id");
        if (id.isEmpty()) continue;
        for (auto& h : t.header) if (h != "id") {const auto v = t.get(r, h); if (v.isNotEmpty()) out[id][h] = v;}
    }
    return out;
}

void Library::setUserValue(const juce::String& id, const juce::String& key, const juce::String& value)
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(userFile());
    if (t.header.isEmpty()) t.header.add("id");
    int r = t.findRow("id", id);
    if (r < 0) {t.rows.push_back(juce::StringArray()); r = (int)t.rows.size() - 1; t.set((size_t)r, "id", id);}
    t.set((size_t)r, key, value.trim());
    t.save(userFile());
    ++changes;
}

std::vector<CustomColumn> Library::customColumns()
{
    const juce::ScopedLock sl(lock);
    std::vector<CustomColumn> out;
    const juce::var v = juce::JSON::parse(columnsFile());
    if (auto* arr = v.getProperty("columns", juce::var()).getArray())
        for (auto& c : *arr) out.push_back({c.getProperty("key", "").toString(), c.getProperty("name", "").toString(), c.getProperty("type", "text").toString() == "number"});
    return out;
}

void Library::saveColumns(const std::vector<CustomColumn>& cols)
{
    juce::Array<juce::var> arr;
    for (auto& c : cols) {auto* o = new juce::DynamicObject(); o->setProperty("key", c.key); o->setProperty("name", c.name); o->setProperty("type", c.numeric ? "number" : "text"); arr.add(juce::var(o));}
    auto* root = new juce::DynamicObject(); root->setProperty("columns", arr);
    columnsFile().replaceWithText(juce::JSON::toString(juce::var(root)));
    ++changes;
}

juce::String Library::addCustomColumn(const juce::String& name, bool numeric)
{
    const juce::ScopedLock sl(lock);
    auto cols = customColumns();
    juce::String base = "c_" + name.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_ ").replaceCharacter(' ', '_');
    if (base == "c_") base = "c_column";
    juce::String key = base; int n = 2;
    for (bool clash = true; clash;) {clash = false; for (auto& c : cols) if (c.key == key) {clash = true; key = base + juce::String(n++);}}
    cols.push_back({key, name.trim(), numeric});
    saveColumns(cols);
    return key;
}

void Library::renameCustomColumn(const juce::String& key, const juce::String& newName)
{
    const juce::ScopedLock sl(lock);
    auto cols = customColumns();
    for (auto& c : cols) if (c.key == key) c.name = newName.trim();
    saveColumns(cols);
}

void Library::removeCustomColumn(const juce::String& key)
{
    const juce::ScopedLock sl(lock);
    auto cols = customColumns();
    cols.erase(std::remove_if(cols.begin(), cols.end(), [&](const CustomColumn& c) { return c.key == key; }), cols.end());
    saveColumns(cols);
    Table t; t.load(userFile()); //drop its values too
    const int c = t.col(key);
    if (c >= 0) {t.header.remove(c); for (auto& r : t.rows) if (c < r.size()) r.remove(c); t.save(userFile());}
}

bool Library::removeTrack(const juce::String& id)
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(indexFile());
    const int r = t.findRow("id", id);
    if (r < 0) return false;
    t.rows.erase(t.rows.begin() + r);
    t.save(indexFile());
    for (auto* ext : {".core.csv", ".fine.f32.gz", ".pitch.f32.gz", ".meta.json"}) folder.getChildFile("tracks").getChildFile(id + ext).deleteFile();
    Table u; u.load(userFile());
    const int ur = u.findRow("id", id);
    if (ur >= 0) {u.rows.erase(u.rows.begin() + ur); u.save(userFile());}
    ++changes;
    return true;
}

juce::String Library::importCsv(const juce::File& csv)
{
    auto all = Table::parse(csv.loadFileAsString());
    if (all.size() < 2) return "That file has no rows to import.";
    juce::StringArray head = all[0];
    for (auto& h : head) h = h.trim();
    int idCol = -1, fileCol = -1;
    for (int i = 0; i < head.size(); ++i) {
        if (head[i].equalsIgnoreCase("id")) idCol = i;
        if (head[i].equalsIgnoreCase("file") || head[i].equalsIgnoreCase("filename") || head[i].equalsIgnoreCase("file name") || head[i].equalsIgnoreCase("track")) fileCol = i;
    }
    if (idCol < 0 && fileCol < 0) return "The CSV needs a column called \"file\" (or \"id\") to match tracks.";
    const Table idx = index();
    auto stem = [](const juce::String& f) { return juce::File::createFileWithoutCheckingPath("/" + f.trim()).getFileNameWithoutExtension().toLowerCase(); };
    std::map<juce::String, juce::String> byStem;
    for (size_t r = 0; r < idx.rows.size(); ++r) byStem[stem(idx.get(r, "file"))] = idx.get(r, "id");
    //where each CSV column goes
    auto cols = customColumns();
    std::vector<juce::String> target(head.size());
    for (int i = 0; i < head.size(); ++i) {
        if (i == idCol || i == fileCol || head[i].isEmpty()) continue;
        juce::String k;
        for (auto& b : editableBuiltIns()) if (head[i].equalsIgnoreCase(b)) k = b;
        if (head[i].equalsIgnoreCase("tempo")) k = "bpm";
        for (auto& c : cols) if (head[i].equalsIgnoreCase(c.name) || head[i] == c.key) k = c.key;
        if (k.isEmpty()) { //a new column of yours: a number column if every value is a number
            bool numeric = true;
            for (size_t r = 1; r < all.size(); ++r) {const auto v = i < all[r].size() ? all[r][i].trim().removeCharacters(",") : juce::String(); if (v.isNotEmpty() && !v.containsOnly("0123456789.-+eE")) numeric = false;}
            k = addCustomColumn(head[i], numeric);
            cols = customColumns();
        }
        target[(size_t)i] = k;
    }
    int matched = 0, unmatched = 0, values = 0;
    juce::StringArray missing;
    for (size_t r = 1; r < all.size(); ++r) {
        juce::String id;
        if (idCol >= 0 && idCol < all[r].size()) id = all[r][idCol].trim();
        if (id.isEmpty() && fileCol >= 0 && fileCol < all[r].size()) {auto it = byStem.find(stem(all[r][fileCol])); if (it != byStem.end()) id = it->second;}
        if (id.isEmpty() || idx.findRow("id", id) < 0) {++unmatched; if (fileCol >= 0 && fileCol < all[r].size() && missing.size() < 8) missing.add(all[r][fileCol]); continue;}
        ++matched;
        for (int i = 0; i < head.size() && i < all[r].size(); ++i)
            if (target[(size_t)i].isNotEmpty() && all[r][i].trim().isNotEmpty()) {setUserValue(id, target[(size_t)i], all[r][i].trim()); ++values;}
    }
    juce::String report = "Imported " + juce::String(values) + " values for " + juce::String(matched) + " tracks.";
    if (unmatched > 0) report << " " << unmatched << " rows did not match a track" << (missing.size() ? ": " + missing.joinIntoString(", ") + (unmatched > missing.size() ? " ..." : "") : juce::String()) << ".";
    return report;
}

//---------------------------------------------------------------- filling in values added by later versions
static bool readCoreCsv(const juce::File& f, std::vector<CoreWindow>& out)
{
    juce::StringArray lines; lines.addLines(f.loadFileAsString());
    for (int i = 1; i < lines.size(); ++i) {
        juce::StringArray c; c.addTokens(lines[i], ",", "");
        if (c.size() < 12) continue;
        CoreWindow w;
        float* v = &w.peakL;
        for (int k = 0; k < 10; ++k) v[k] = c[k + 2].getFloatValue();
        out.push_back(w);
    }
    return !out.empty();
}

static bool readFloats(const juce::File& f, std::vector<float>& out)
{
    juce::FileInputStream fis(f);
    if (!fis.openedOk()) return false;
    juce::GZIPDecompressorInputStream gz(&fis, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb; gz.readIntoMemoryBlock(mb);
    out.resize(mb.getSize()/4);
    juce::MemoryInputStream m(mb, false);
    for (auto& v : out) v = m.readFloat();
    return !out.empty();
}

int Library::upgradeOldRows(std::function<bool()> shouldStop)
{
    Table t = index();
    if (t.rows.empty()) return 0;
    int changed = 0;
    for (size_t r = 0; r < t.rows.size(); ++r) {
        if (shouldStop && shouldStop()) break;
        const juce::String id = t.get(r, "id");
        const juce::File tracks = folder.getChildFile("tracks");
        bool dirty = false;
        if (t.get(r, "letter_value").isEmpty()) {
            std::vector<CoreWindow> core;
            if (readCoreCsv(tracks.getChildFile(id + ".core.csv"), core)) {
                OriginalReplay rep; for (auto& w : core) rep.push(w);
                const auto o = rep.result();
                t.set(r, "letter_value", num(o.letterValue, 3)); t.set(r, "bonus_value", juce::String(o.bonusValue)); dirty = true;
            }
        }
        if (t.get(r, "bpm_confidence").isEmpty()) { //never worked out (an empty BPM with a confidence means "no clear beat")
            std::vector<float> fine;
            const juce::var meta = juce::JSON::parse(tracks.getChildFile(id + ".meta.json"));
            const double sr = meta.getProperty("sample_rate", 0.0), blk = meta.getProperty("fine_block_samples", 0.0);
            if (sr > 0 && blk > 0 && readFloats(tracks.getChildFile(id + ".fine.f32.gz"), fine) && fine.size() % F_NUM_COLS == 0) {
                const auto b = estimateBpm(fine, blk/sr);
                t.set(r, "bpm_est", b.bpm > 0 ? num(b.bpm, 1) : juce::String()); t.set(r, "bpm_confidence", num(b.confidence, 2)); dirty = true;
            }
        }
        if (dirty) {
            juce::StringArray cells;
            juce::StringArray want; for (auto c : kIndexHeader) want.add(c);
            for (auto& c : want) cells.add(t.get(r, c));
            putIndexRow(cells);
            ++changed;
        }
    }
    return changed;
}

void Library::writeReadme()
{
    const juce::File f = folder.getChildFile("README.txt");
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
        "tracks/<id>.pitch.f32.gz  every ~93 ms: how strongly each of the 12 notes sounds (C..B), then the RMS (13 float32 per row).\r\n"
        "tracks/<id>.meta.json  everything about the file and how it was captured, including all its tags.\r\n\r\n"
        "user.csv               YOUR values, by track id: tag fixes, BPM/key corrections, your own columns. Scanning never changes it.\r\n"
        "columns.json           the names and types of your own columns.\r\n\r\n"
        "The id is part of a SHA-256 of the file, so adding the same file twice is skipped. Delete a track's files and its index row to remove it.\r\n",
        false, false, "\r\n");
}

} // namespace f8k
