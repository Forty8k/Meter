#include "Engine.h"
#include "Capture.h"
#include "OriginalScorer.h"
#include "Estimates.h"
#include "LabHints.h"
#include <juce_cryptography/juce_cryptography.h>
#include <thread>

namespace mdark {

static const char* kIndexHeader[] = {"id", "file", "artist", "title", "album", "year", "genre", "format", "lossy", "sample_rate", "bits", "channels",
    "duration_s", "peak_dbfs", "true_peak_dbtp", "lufs_integrated", "bpm_est", "bpm_confidence", "key_est", "key_confidence", "analysed", "capture_version", "source_path"};

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
    ChangeNote n; n.kind = ChangeNote::IndexRow; n.id = cells[0]; n.header = want; n.cells = cells; notify(n);
}

//---------------------------------------------------------------- scanning
static const char* kGradeHeader[] = {"id", "grade", "letter", "bonus", "authority", "power", "detail", "directions_main", "directions_end",
    "authority_value", "power_value", "detail_value", "letter_value", "bonus_value", "windows", "scored"};

juce::File Library::gradesFile(const juce::String& vid, int rate) const { return folder.getChildFile("grades-" + vid + (rate > 0 ? "@" + juce::String(rate) : juce::String()) + ".csv"); }
juce::File Library::rateCoreFile(const juce::String& id, const juce::String& vid, int rate) const { return folder.getChildFile("tracks").getChildFile(id + ".core-" + vid + "@" + juce::String(rate) + ".f32.gz"); }

const std::vector<int>& Library::anchorRates() { static const std::vector<int> r{44100, 96000}; return r; }
juce::File Library::coreFile(const juce::String& id, const juce::String& vid) const { return folder.getChildFile("tracks").getChildFile(id + ".core-" + vid + ".csv"); }

std::vector<juce::String> Library::versionIds()
{
    std::vector<juce::String> v;
    for (auto& m : meterVersions()) v.push_back(juce::String(m.id));
    return v;
}

juce::String Library::windowSource(const juce::String& vid)
{
    const MeterVersion* mv = findMeterVersion(vid.toStdString());
    return (mv != nullptr && !mv->sameWindowsAs.empty()) ? juce::String(mv->sameWindowsAs) : vid;
}

void Library::putGradeRow(const juce::String& vid, const juce::String& id, const OriginalResult& o, int rate)
{
    putGradeRows(vid, rate, {{id, o}});
}

void Library::putGradeRows(const juce::String& vid, int rate, const std::vector<std::pair<juce::String, OriginalResult>>& rows)
{
    if (rows.empty()) return;
    const juce::ScopedLock sl(lock);
    Table t; t.load(gradesFile(vid, rate));
    juce::StringArray want; for (auto c : kGradeHeader) want.add(c);
    if (t.header != want) {Table n; n.header = want; for (size_t r = 0; r < t.rows.size(); ++r) {n.rows.push_back({}); for (auto& c : want) n.rows.back().add(t.get(r, c));} t = n;}
    std::map<juce::String, size_t> rowOf; for (size_t r = 0; r < t.rows.size(); ++r) rowOf[t.get(r, "id")] = r;
    const juce::String now = juce::Time::getCurrentTime().toISO8601(true);
    std::vector<juce::StringArray> written;
    for (auto& [id, o] : rows) {
        const juce::StringArray cells{id, juce::String(o.grade), juce::String(o.letter), juce::String(o.bonus), juce::String(o.authority), juce::String(o.power),
            juce::String(o.detail), juce::String(o.directionsMain), juce::String(o.directions), num(o.bassPrevGrade, 3), num(o.peakPrevGrade, 3), num(o.slewPrevGrade, 3),
            num(o.letterValue, 3), juce::String(o.bonusValue), juce::String((int)o.windows), now};
        auto it = rowOf.find(id);
        if (it != rowOf.end()) t.rows[it->second] = cells; else { rowOf[id] = t.rows.size(); t.rows.push_back(cells); }
        written.push_back(cells);
    }
    t.save(gradesFile(vid, rate));
    for (size_t i = 0; i < rows.size(); ++i) { ChangeNote n; n.kind = ChangeNote::GradeRow; n.id = rows[i].first; n.version = vid; n.rate = rate; n.header = want; n.cells = written[i]; notify(n); }
}

Table Library::gradeFile(const juce::String& vid, int rate)
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(gradesFile(vid, rate));
    return t;
}

Table Library::grades(const juce::String& vid, int rate)
{
    const juce::ScopedLock sl(lock);
    Table t; t.load(gradesFile(vid));
    if (rate <= 0) return t;
    Table r; r.load(gradesFile(vid, rate));
    if (r.header.isEmpty()) r.header = t.header;
    if (r.header.isEmpty()) return r;
    //tracks that already are at this rate were not converted: their own grades are the answer
    Table idx; idx.load(indexFile());
    std::map<juce::String, size_t> have, nat;
    for (size_t i = 0; i < r.rows.size(); ++i) have[r.get(i, "id")] = i;
    for (size_t i = 0; i < t.rows.size(); ++i) nat[t.get(i, "id")] = i;
    for (size_t i = 0; i < idx.rows.size(); ++i) {
        if (juce::roundToInt(idx.get(i, "sample_rate").getDoubleValue()) != rate) continue;
        const juce::String id = idx.get(i, "id");
        auto n = nat.find(id);
        if (n == nat.end() || have.count(id)) continue;
        juce::StringArray cells;
        for (auto& h : r.header) cells.add(t.get(n->second, h));
        r.rows.push_back(cells);
    }
    return r;
}

juce::File Library::levelGradesFile(const juce::String& vid, int rate, const LevelMode& lm) const
{
    return folder.getChildFile("levelgrades-" + vid + (rate > 0 ? "@" + juce::String(rate) : juce::String()) + "~" + lm.key() + ".csv");
}

Table Library::levelGrades(const juce::String& vid, int rate, const LevelMode& lm, bool compute, std::function<bool()> shouldStop, std::function<void(int, int)> progress, int* missing)
{
    if (lm.mode == 0) {if (missing) *missing = 0; return grades(vid, rate);}
    const Table base = grades(vid, rate);
    Table cache;
    { const juce::ScopedLock sl(lock); cache.load(levelGradesFile(vid, rate, lm)); }
    juce::StringArray want; for (auto c : kGradeHeader) want.add(c);
    if (cache.header != want) {cache = Table(); cache.header = want;}
    std::map<juce::String, size_t> have;
    for (size_t i = 0; i < cache.rows.size(); ++i) have[cache.get(i, "id")] = i;
    Table out; out.header = want;
    std::vector<size_t> todo; //base rows that need working out: not kept yet, or graded again since
    for (size_t r = 0; r < base.rows.size(); ++r) {
        auto it = have.find(base.get(r, "id"));
        if (it != have.end() && cache.get(it->second, "scored") == base.get(r, "scored")) out.rows.push_back(cache.rows[it->second]);
        else todo.push_back(r);
    }
    if (missing) *missing = (int)todo.size();
    if (!compute || todo.empty()) return out;
    Table idx = index();
    std::map<juce::String, double> srOf;
    for (size_t i = 0; i < idx.rows.size(); ++i) srOf[idx.get(i, "id")] = idx.get(i, "sample_rate").getDoubleValue();
    const MeterVersion* mv = findMeterVersion(vid.toStdString());
    if (mv == nullptr) return out;
    std::atomic<int> next{0}, done{0};
    juce::CriticalSection mine;
    std::vector<juce::StringArray> made;
    auto work = [&] {
        for (int k; (k = next++) < (int)todo.size();) {
            if (shouldStop && shouldStop()) return;
            const size_t r = todo[(size_t)k];
            const juce::String id = base.get(r, "id");
            std::vector<std::string> names; std::vector<float> values;
            if (readCoreAt(id, vid, rate, names, values) && !names.empty()) {
                const double sr = rate > 0 ? (double)rate : srOf[id];
                const std::vector<float> scaled = lm.mode == 1 ? scaleWindows(names, values, 20.0*std::log10(lm.gain()))
                                                               : scaleWindowsEach(names, values, mixWindowGains(names, values, sr));
                auto run = mv->create(sr, 1);
                for (size_t w = 0; w + names.size() <= scaled.size(); w += names.size()) run->feedWindow(names, &scaled[w]);
                const OriginalResult o = run->result();
                const juce::StringArray cells{id, juce::String(o.grade) + lm.suffix(), juce::String(o.letter), juce::String(o.bonus), juce::String(o.authority), juce::String(o.power),
                    juce::String(o.detail), juce::String(o.directionsMain), juce::String(o.directions), num(o.bassPrevGrade, 3), num(o.peakPrevGrade, 3), num(o.slewPrevGrade, 3),
                    num(o.letterValue, 3), juce::String(o.bonusValue), juce::String((int)o.windows), base.get(r, "scored")};
                const juce::ScopedLock sl(mine); made.push_back(cells);
            }
            const int d = ++done;
            if (progress) progress(d, (int)todo.size());
        }
    };
    const int nThreads = juce::jmax(1, (int)std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> th;
    for (int i = 0; i < nThreads; ++i) th.emplace_back(work);
    for (auto& t : th) t.join();
    { //keep what was worked out (also when stopped half way), then answer with everything fresh
        for (auto& c : made) {
            auto it = have.find(c[0]);
            if (it != have.end()) cache.rows[it->second] = c; else {have[c[0]] = cache.rows.size(); cache.rows.push_back(c);}
            out.rows.push_back(c);
        }
        const juce::ScopedLock sl(lock); cache.save(levelGradesFile(vid, rate, lm));
    }
    return out;
}

static void writeCore(const juce::File& f, const MeterRun& run, double sr)
{
    juce::String csv;
    csv.preallocateBytes(run.core.size() * 14 + 200);
    csv << "window,time_s";
    for (auto& n : run.names) csv << "," << juce::String(n);
    csv << "\r\n";
    const int win = (int)std::floor((1881.0/44100.0)*sr) + 1;
    const size_t k = run.names.size();
    char num[48];
    for (size_t w = 0; w < run.windowCount(); ++w) {
        snprintf(num, sizeof num, "%zu,%.6f", w, (double)(w + 1)*win/sr);
        csv << num;
        for (size_t i = 0; i < k; ++i) {snprintf(num, sizeof num, ",%.9g", run.core[w*k + i]); csv << num;}
        csv << "\r\n";
    }
    f.replaceWithText(csv, false, false, "\r\n");
}

// The window data at a converted rate: float32 rows (one per window, the version's messages in order), gzip compressed. The column names live in meta.json.
static void writeCoreGz(const juce::File& f, const MeterRun& run)
{
    f.deleteFile();
    juce::FileOutputStream fos(f);
    if (!fos.openedOk()) return;
    juce::GZIPCompressorOutputStream gz(fos, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
    for (float v : run.core) gz.writeFloat(v);
    gz.flush();
}

static bool readFloats(const juce::File& f, std::vector<float>& out);

bool Library::readCoreAt(const juce::String& id, const juce::String& vid, int rate, std::vector<std::string>& names, std::vector<float>& values)
{
    if (rate <= 0) return readCore(id, vid, names, values);
    const juce::var meta = juce::JSON::parse(folder.getChildFile("tracks").getChildFile(id + ".meta.json"));
    if (juce::roundToInt((double)meta.getProperty("sample_rate", 0.0)) == rate) return readCore(id, vid, names, values);
    if (auto* arr = meta.getProperty("rate_runs", juce::var()).getArray())
        for (auto& r : *arr) {
            if (juce::roundToInt((double)r.getProperty("rate", 0.0)) != rate || r.getProperty("core_version", "").toString() != vid) continue;
            names.clear(); values.clear();
            if (auto* nm = r.getProperty("core_columns", juce::var()).getArray()) for (auto& n : *nm) names.push_back(n.toString().toStdString());
            return !names.empty() && readFloats(rateCoreFile(id, vid, rate), values) && values.size() % names.size() == 0;
        }
    const juce::String src = windowSource(vid);
    if (src != vid) return readCoreAt(id, src, rate, names, values); //a version that measures exactly like an older one: that one's stored windows
    return false;
}

// Reads a stored core file. Files from the first analyser versions (<id>.core.csv) hold Meter 0.5.5's ten values under short names.
bool Library::readCore(const juce::String& id, const juce::String& vid, std::vector<std::string>& names, std::vector<float>& values)
{
    juce::File f = coreFile(id, vid);
    bool legacy = false;
    if (!f.existsAsFile() && vid == juce::String(meterVersions().front().id)) {f = folder.getChildFile("tracks").getChildFile(id + ".core.csv"); legacy = true;}
    if (!f.existsAsFile()) { const juce::String src = windowSource(vid); return src != vid && readCore(id, src, names, values); } //measures exactly like an older version
    juce::StringArray lines; lines.addLines(f.loadFileAsString());
    if (lines.size() < 2) return false;
    juce::StringArray head; head.addTokens(lines[0], ",", "");
    names.clear(); values.clear();
    static const char* legacyNames[] = {"PEAK_LEFT", "PEAK_RIGHT", "SLEW_LEFT", "SLEW_RIGHT", "ZERO_LEFT", "ZERO_RIGHT", "BEAM_LEFT", "BEAM_RIGHT", "DEPT_LEFT", "DEPT_RIGHT"};
    for (int i = 2; i < head.size(); ++i) names.push_back(legacy ? std::string(legacyNames[std::min(i - 2, 9)]) : head[i].toStdString());
    const size_t k = names.size();
    for (int l = 1; l < lines.size(); ++l) {
        juce::StringArray c; c.addTokens(lines[l], ",", "");
        if ((size_t)c.size() < k + 2) continue;
        for (size_t i = 0; i < k; ++i) values.push_back(c[(int)i + 2].getFloatValue());
    }
    return !values.empty();
}

// One way of presenting the audio to the Meter versions: as the file has it, or converted to an anchor rate first.
struct Library::Lane {
    int rate = 0;                    // 0 = as the file has it, else the anchor rate
    std::vector<std::unique_ptr<MeterRun>> runs; // one per Meter version asked for
    std::unique_ptr<mdarkdsp::Resampler> rl, rr;
    std::vector<float> bl, br;
    size_t skip = 0;                 // converted samples still to drop (the filter's delay), so the converted audio lines up with the original
    void feed(const float* L, const float* R, int n, bool mono)
    {
        if (!rl) {for (auto& r : runs) r->process(L, R, n); return;}
        bl.clear(); rl->process(L, (size_t)n, bl);
        if (mono) br = bl; else {br.clear(); rr->process(R, (size_t)n, br);}
        push();
    }
    void finish(bool mono)
    {
        if (!rl) return;
        bl.clear(); rl->flush(bl);
        if (mono) br = bl; else {br.clear(); rr->flush(br);}
        push();
    }
    void push()
    {
        size_t off = std::min(skip, bl.size()); skip -= off;
        if (bl.size() > off && br.size() == bl.size()) for (auto& r : runs) r->process(bl.data() + off, br.data() + off, (int)(bl.size() - off));
    }
};

// A lane for the file's own rate (rate 0) and one for each anchor rate it is not already at.
// rate 0 = the file's own rate. Returns false when that conversion is not possible.
static bool addLane(std::vector<Library::Lane>& lanes, double fileRate, int rate, const std::vector<juce::String>& vids)
{
    Library::Lane ln; ln.rate = rate;
    const double runRate = rate > 0 ? (double)rate : fileRate;
    if (rate > 0) {
        ln.rl = std::make_unique<mdarkdsp::Resampler>(fileRate, (double)rate);
        ln.rr = std::make_unique<mdarkdsp::Resampler>(fileRate, (double)rate);
        if (!ln.rl->ok()) return false;
        ln.skip = (size_t)std::llround(ln.rl->latencyInputSamples()*(double)rate/fileRate);
    }
    for (auto& v : vids) if (auto* mv = findMeterVersion(v.toStdString())) ln.runs.push_back(mv->create(runRate, 1));
    lanes.push_back(std::move(ln));
    return true;
}

// The native lane, then a lane for each anchor rate the file is not already at.
static std::vector<Library::Lane> makeLanes(double fileRate, const std::vector<juce::String>& vids)
{
    std::vector<Library::Lane> lanes;
    addLane(lanes, fileRate, 0, vids);
    for (int a : Library::anchorRates()) if (juce::roundToInt(fileRate) != a) addLane(lanes, fileRate, a, vids);
    return lanes;
}

// Decodes the audio once and feeds it to the extras capture (if wanted) and to every lane (each Meter version's own audio loop).
bool Library::scanAudio(const juce::File& audio, Capture* cap, std::vector<Lane>& lanes, std::function<void(double)> progress,
                        std::function<bool()> shouldStop, juce::String& error, std::unique_ptr<juce::AudioFormatReader>* readerOut)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats(); //one per scan, so several scans can run side by side
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(audio));
    if (reader == nullptr) {error = "could not open this file as audio"; return false;}
    const int numCh = (int)reader->numChannels;
    const juce::int64 length = reader->lengthInSamples;
    if (reader->sampleRate < 8000.0 || numCh < 1 || length < 1) {error = "no usable audio in this file"; return false;}
    const int chunk = 65536;
    const bool mono = numCh < 2; //mono plays as dual mono, as on a stereo track
    juce::AudioBuffer<float> buf(juce::jmax(2, numCh), chunk);
    for (juce::int64 pos = 0; pos < length; pos += chunk) {
        if (shouldStop && shouldStop()) {error = "stopped"; return false;}
        const int n = (int)juce::jmin<juce::int64>(chunk, length - pos);
        buf.clear();
        reader->read(&buf, 0, n, pos, true, numCh > 1);
        const float* L = buf.getReadPointer(0);
        const float* R = numCh > 1 ? buf.getReadPointer(1) : L;
        if (cap) cap->process(L, R, n);
        for (auto& ln : lanes) ln.feed(L, R, n, mono);
        if (progress) progress((double)(pos + n) / (double)length * 0.9);
    }
    for (auto& ln : lanes) ln.finish(mono);
    if (cap) cap->finish();
    if (readerOut) *readerOut = std::move(reader);
    return true;
}

void Library::refreshDoneCache()
{
    if (doneCache.change == changes.load()) return;
    doneCache.extrasCurrent.clear(); doneCache.gradeIds.clear();
    const Table t = index();
    const double cur = juce::String(kCaptureVersion).getDoubleValue();
    for (size_t r = 0; r < t.rows.size(); ++r) doneCache.extrasCurrent[t.get(r, "id")] = t.get(r, "capture_version").getDoubleValue() >= cur;
    for (auto& v : versionIds()) {
        const Table g = grades(v);
        auto& ids = doneCache.gradeIds[v];
        for (size_t r = 0; r < g.rows.size(); ++r) ids.insert(g.get(r, "id"));
    }
    doneCache.change = changes.load();
}
bool Library::isComplete(const juce::String& id)
{
    auto e = doneCache.extrasCurrent.find(id);
    if (e == doneCache.extrasCurrent.end() || !e->second) return false;
    for (auto& v : versionIds()) if (doneCache.gradeIds[v].count(id) == 0) return false;
    return true;
}
void Library::loadKnown()
{
    if (knownLoaded) return;
    knownLoaded = true;
    const juce::File f = knownFile();
    if (!f.existsAsFile()) return;
    juce::StringArray lines; lines.addLines(f.loadFileAsString());
    for (auto& ln : lines) { //path,size,mtime,id (the path is the first field and may hold commas, so the last three are cut off from the right)
        const int c3 = ln.lastIndexOfChar(','); if (c3 < 1) continue;
        const int c2 = ln.substring(0, c3).lastIndexOfChar(','); if (c2 < 1) continue;
        const int c1 = ln.substring(0, c2).lastIndexOfChar(','); if (c1 < 1) continue;
        KnownFile k; k.size = ln.substring(c1 + 1, c2).getLargeIntValue(); k.mtime = ln.substring(c2 + 1, c3).getLargeIntValue(); k.id = ln.substring(c3 + 1).trim();
        if (k.id.isNotEmpty()) known[ln.substring(0, c1)] = k;
    }
}
void Library::rememberKnown(const juce::File& audio, const juce::String& id)
{
    const juce::ScopedLock sl(lock);
    loadKnown();
    KnownFile k; k.size = audio.getSize(); k.mtime = audio.getLastModificationTime().toMilliseconds(); k.id = id;
    const juce::String path = audio.getFullPathName();
    auto it = known.find(path);
    if (it != known.end() && it->second.size == k.size && it->second.mtime == k.mtime && it->second.id == id) return;
    known[path] = k;
    juce::FileOutputStream os(knownFile());
    if (os.openedOk()) { os.setPosition(os.getFile().getSize()); os.writeText(path + "," + juce::String(k.size) + "," + juce::String(k.mtime) + "," + id + "\n", false, false, nullptr); } //newest line wins
}

AnalyseOutcome Library::analyse(const juce::File& audio, std::function<void(double)> progress, std::function<bool()> shouldStop)
{
    {   // the fast way out: the same file as when it was added, and the library already has everything for it
        const juce::ScopedLock sl(lock);
        loadKnown();
        auto it = known.find(audio.getFullPathName());
        if (it != known.end() && it->second.size == audio.getSize() && it->second.mtime == audio.getLastModificationTime().toMilliseconds()) {
            refreshDoneCache();
            if (isComplete(it->second.id)) {
                AnalyseOutcome out; out.id = it->second.id; out.status = AnalyseOutcome::AlreadyThere; out.message = "already in the library"; return out;
            }
        }
    }
    AnalyseOutcome out = analyseHashed(audio, progress, shouldStop);
    if (out.status != AnalyseOutcome::Failed && out.id.isNotEmpty()) rememberKnown(audio, out.id);
    return out;
}

AnalyseOutcome Library::analyseHashed(const juce::File& audio, std::function<void(double)> progress, std::function<bool()> shouldStop)
{
    AnalyseOutcome out;
    const juce::String id = juce::SHA256(audio).toHexString().substring(0, 16); //the same file content always gets the same id
    out.id = id;
    bool replacing = false;
    std::vector<juce::String> missing; //Meter versions this track has no grade from yet
    bool inIndex = false, extrasCurrent = false;
    {
        const juce::ScopedLock sl(lock);
        refreshDoneCache(); //kept in memory: the CSV files are only read again when something in the library changed
        for (auto& v : versionIds()) if (doneCache.gradeIds[v].count(id) == 0) missing.push_back(v);
        const auto e = doneCache.extrasCurrent.find(id);
        if (e != doneCache.extrasCurrent.end()) {inIndex = true; extrasCurrent = e->second;}
    }
    {
        if (inIndex) {
            if (extrasCurrent && missing.empty()) {out.status = AnalyseOutcome::AlreadyThere; out.message = "already in the library"; return out;}
            if (extrasCurrent) { //only the newer Meter versions are missing
                //a version that measures exactly like an older one is graded from the stored windows (no audio needed)
                std::vector<juce::String> needAudio;
                for (auto& v : missing) {
                    std::vector<std::string> names; std::vector<float> values;
                    if (windowSource(v) != v && readCore(id, v, names, values) && !names.empty()) {
                        const MeterVersion* mv = findMeterVersion(v.toStdString());
                        double fileRate = 44100.0;
                        { Table ix = index(); const int r = ix.findRow("id", id); if (r >= 0) fileRate = ix.get((size_t)r, "sample_rate").getDoubleValue(); }
                        auto run = mv->create(fileRate, 1);
                        for (size_t w = 0; w + names.size() <= values.size(); w += names.size()) run->feedWindow(names, &values[w]);
                        putGradeRow(v, id, run->result());
                        for (int a : anchorRates()) if (a != juce::roundToInt(fileRate) && readCoreAt(id, v, a, names, values) && !names.empty()) {
                            auto ra = mv->create((double)a, 1);
                            for (size_t w = 0; w + names.size() <= values.size(); w += names.size()) ra->feedWindow(names, &values[w]);
                            putGradeRow(v, id, ra->result(), a);
                        }
                    } else needAudio.push_back(v);
                }
                if (!needAudio.empty() && !scanVersions(id, audio, needAudio, progress, shouldStop)) {out.message = "could not read the audio again"; return out;}
                out.status = AnalyseOutcome::Updated; out.message = "updated with Meter " + juce::String(missing.size() == 1 ? findMeterVersion(missing[0].toStdString())->version : "versions");
                return out;
            }
            replacing = true; //scanned by an older analyser: scan again and update it in place (your own values are kept)
        }
    }
    missing = versionIds();

    std::unique_ptr<juce::AudioFormatReader> reader;
    std::vector<Lane> lanes;
    std::unique_ptr<Capture> capPtr;
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> probe(fm.createReaderFor(audio));
        if (probe == nullptr) {out.message = "could not open this file as audio"; return out;}
        lanes = makeLanes(probe->sampleRate, versionIds());
        capPtr = std::make_unique<Capture>(probe->sampleRate);
    }
    juce::String error;
    Capture& cap = *capPtr;
    if (!scanAudio(audio, &cap, lanes, progress, shouldStop, error, &reader)) {out.message = error; return out;}
    auto& runs = lanes[0].runs; //the file's own rate
    const double sr = reader->sampleRate;
    const int numCh = (int)reader->numChannels;
    const juce::int64 length = reader->lengthInSamples;
    const BpmEstimate bpm = estimateBpm(cap.fine, cap.fineBlockSamples/sr);
    const KeyEstimate key = estimateKey(cap.pitch.frames);

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
    //CORE and GRADES: for each Meter version, its own window data and its verdict
    std::vector<OriginalResult> results;
    for (size_t i = 0; i < runs.size(); ++i) {
        const juce::String vid(meterVersions()[i].id);
        writeCore(coreFile(id, vid), *runs[i], sr);
        results.push_back(runs[i]->result());
        putGradeRow(vid, id, results.back());
    }
    //the same audio converted to each anchor rate: every version's grade, and the newest version's window data
    for (size_t li = 1; li < lanes.size(); ++li) {
        for (size_t i = 0; i < lanes[li].runs.size(); ++i) putGradeRow(juce::String(meterVersions()[i].id), id, lanes[li].runs[i]->result(), lanes[li].rate);
        writeCoreGz(rateCoreFile(id, juce::String(meterVersions().back().id), lanes[li].rate), *lanes[li].runs.back());
    }
    for (int a : anchorRates()) if (juce::roundToInt(sr) == a || std::none_of(lanes.begin() + 1, lanes.end(), [&](const Lane& l) { return l.rate == a; })) {
        for (auto& v : versionIds()) rateCoreFile(id, v, a).deleteFile(); //nothing stale left from an earlier scan
    }
    tracks.getChildFile(id + ".core.csv").deleteFile(); //the first analyser versions' file, now replaced by core-<version>.csv
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
    const double lufs = cap.integratedLufs();
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
        juce::Array<juce::var> mv;
        for (size_t i = 0; i < runs.size(); ++i) {
            auto* v = new juce::DynamicObject();
            v->setProperty("id", juce::String(meterVersions()[i].id)); v->setProperty("version", juce::String(meterVersions()[i].version));
            v->setProperty("commit", juce::String(meterVersions()[i].commit)); v->setProperty("core_file", id + ".core-" + juce::String(meterVersions()[i].id) + ".csv");
            juce::Array<juce::var> nm; for (auto& n : runs[i]->names) nm.add(juce::String(n));
            v->setProperty("core_columns", nm); v->setProperty("core_windows", (int)runs[i]->windowCount());
            v->setProperty("grade", juce::String(results[i].grade));
            mv.add(juce::var(v));
        }
        o->setProperty("meter_versions", mv);
        {   //the same audio converted to each anchor rate (a rate the file already has is not repeated: its own data is used)
            juce::Array<juce::var> rr;
            for (size_t li = 1; li < lanes.size(); ++li) {
                auto* r = new juce::DynamicObject();
                const int rate = lanes[li].rate;
                const auto& last = *lanes[li].runs.back();
                r->setProperty("rate", rate);
                juce::Array<juce::var> gs;
                for (size_t i = 0; i < lanes[li].runs.size(); ++i) {auto* g = new juce::DynamicObject(); g->setProperty("id", juce::String(meterVersions()[i].id)); g->setProperty("grade", juce::String(lanes[li].runs[i]->result().grade)); gs.add(juce::var(g));}
                r->setProperty("grades", gs);
                r->setProperty("core_version", juce::String(meterVersions().back().id));
                r->setProperty("core_file", rateCoreFile(id, juce::String(meterVersions().back().id), rate).getFileName());
                juce::Array<juce::var> nm; for (auto& n : last.names) nm.add(juce::String(n));
                r->setProperty("core_columns", nm); r->setProperty("core_windows", (int)last.windowCount());
                r->setProperty("core_window_samples", (int)std::floor((1881.0/44100.0)*rate) + 1);
                rr.add(juce::var(r));
            }
            o->setProperty("rate_runs", rr);
        }
        o->setProperty("core_window_samples", (int)std::floor((1881.0/44100.0)*sr) + 1);
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
        if (std::isfinite(lufs)) o->setProperty("lufs_integrated", lufs);
        o->setProperty("analysed", juce::Time::getCurrentTime().toISO8601(true));
        tracks.getChildFile(id + ".meta.json").replaceWithText(juce::JSON::toString(meta), false, false, "\n");
    }
    putIndexRow({id, audio.getFileName(), artist, title, album, year, genre, reader->getFormatName(), lossy ? "yes" : "no",
        num(sr, 0), juce::String((int)reader->bitsPerSample), juce::String(numCh), num((double)length/sr, 2),
        num(20.0*std::log10(std::max(cap.peakAbs, 1e-12)), 2), num(20.0*std::log10(std::max(cap.truePeakAbs, 1e-12)), 2), num(lufs, 2),
        bpm.bpm > 0 ? num(bpm.bpm, 1) : juce::String(), num(bpm.confidence, 2), juce::String(key.name), num(key.confidence, 3),
        juce::Time::getCurrentTime().toISO8601(true), juce::String(kCaptureVersion), audio.getFullPathName()});
    if (progress) progress(1.0);
    out.status = replacing ? AnalyseOutcome::Updated : AnalyseOutcome::Added;
    const auto& latest = results.back();
    out.message = juce::String(replacing ? "updated" : "added") + ", Meter " + juce::String(meterVersions().back().version) + " reads " + juce::String(latest.grade.empty() ? std::string("(no grade)") : latest.grade)
                + (bpm.bpm > 0 ? ", ~" + juce::String(juce::roundToInt(bpm.bpm)) + " BPM" : juce::String()) + (key.name.empty() ? juce::String() : ", " + juce::String(key.name));
    return out;
}

// Runs only the given Meter versions over a track already in the library (for versions added after it was scanned),
// at the file's own rate and at each anchor rate.
bool Library::scanVersions(const juce::String& id, const juce::File& audio, const std::vector<juce::String>& vids, std::function<void(double)> progress, std::function<bool()> shouldStop)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> probe(fm.createReaderFor(audio));
    if (probe == nullptr) return false;
    const double sr = probe->sampleRate;
    probe.reset();
    std::vector<Lane> lanes = makeLanes(sr, vids);
    juce::String error;
    if (!scanAudio(audio, nullptr, lanes, progress, shouldStop, error, nullptr)) return false;
    for (auto& ln : lanes)
        for (size_t i = 0; i < ln.runs.size(); ++i) {
            if (ln.rate == 0) writeCore(coreFile(id, vids[i]), *ln.runs[i], sr);
            else if (vids[i] == juce::String(meterVersions().back().id)) writeCoreGz(rateCoreFile(id, vids[i], ln.rate), *ln.runs[i]);
            putGradeRow(vids[i], id, ln.runs[i]->result(), ln.rate);
        }
    return true;
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
    static const juce::StringArray k{"artist", "title", "album", "year", "genre", "bpm", "key", "source"};
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
    ChangeNote n; n.kind = ChangeNote::UserValue; n.id = id; n.key = key; n.value = value.trim(); notify(n);
}

void Library::setUserValues(const std::vector<UserEdit>& edits)
{
    if (edits.empty()) return;
    const juce::ScopedLock sl(lock);
    Table t; t.load(userFile());
    if (t.header.isEmpty()) t.header.add("id");
    std::map<juce::String, int> rowOf;
    for (size_t r = 0; r < t.rows.size(); ++r) rowOf[t.get(r, "id")] = (int)r;
    for (auto& e : edits) {
        auto it = rowOf.find(e.id); int r;
        if (it == rowOf.end()) {t.rows.push_back(juce::StringArray()); r = (int)t.rows.size() - 1; t.set((size_t)r, "id", e.id); rowOf[e.id] = r;} else r = it->second;
        t.set((size_t)r, e.key, e.value.trim());
    }
    t.save(userFile());
    for (auto& e : edits) { ChangeNote n; n.kind = ChangeNote::UserValue; n.id = e.id; n.key = e.key; n.value = e.value.trim(); notify(n); }
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
    ChangeNote n; n.kind = ChangeNote::Columns; notify(n);
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
    if (c >= 0) {t.header.remove(c); for (auto& r : t.rows) if (c < r.size()) r.remove(c); t.save(userFile()); ChangeNote n; n.kind = ChangeNote::ReloadUser; notify(n);}
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
    leanFile(id).deleteFile();
    for (auto& v : versionIds()) {
        coreFile(id, v).deleteFile();
        for (int rate : {0, 44100, 96000}) {
            if (rate > 0) rateCoreFile(id, v, rate).deleteFile();
            Table gt; gt.load(gradesFile(v, rate));
            const int gr = gt.findRow("id", id);
            if (gr >= 0) {gt.rows.erase(gt.rows.begin() + gr); gt.save(gradesFile(v, rate));}
        }
    }
    Table u; u.load(userFile());
    const int ur = u.findRow("id", id);
    if (ur >= 0) {u.rows.erase(u.rows.begin() + ur); u.save(userFile());}
    ChangeNote n; n.kind = ChangeNote::Removed; n.id = id; notify(n);
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

static std::vector<uint8_t> gzipBytes(const std::vector<uint8_t>& in)
{
    juce::MemoryBlock mb;
    { juce::MemoryOutputStream mo(mb, false); juce::GZIPCompressorOutputStream gz(mo, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP); gz.write(in.data(), in.size()); gz.flush(); }
    return std::vector<uint8_t>((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize());
}

bool Library::readLean(const juce::String& id, LeanRecord& out)
{
    juce::FileInputStream fis(leanFile(id));
    if (!fis.openedOk()) return false;
    juce::GZIPDecompressorInputStream gz(&fis, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb; gz.readIntoMemoryBlock(mb);
    return parseLean(std::vector<uint8_t>((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize()), out);
}

static const char* kPrivateKeys[] = {"source_path", "file", "path", "folder", "filename"};

juce::String Library::fileTitle(const juce::String& file)
{
    const juce::String n = file.fromLastOccurrenceOf("/", false, false).fromLastOccurrenceOf("\\", false, false);
    return n.containsChar('.') ? n.upToLastOccurrenceOf(".", false, false) : n;
}

int Library::exportPack(const juce::File& out, bool full, const std::vector<juce::String>& ids, bool names)
{
    std::vector<uint8_t> all;
    all.push_back('P'); all.push_back('C'); all.push_back('K'); all.push_back(1); all.push_back(full ? 1 : 0);
    Table idx; std::map<juce::String, std::map<juce::String, juce::String>> usr; std::map<juce::String, size_t> rowOf;
    if (names) { idx = index(); usr = userValues(); for (size_t r = 0; r < idx.rows.size(); ++r) rowOf[idx.get(r, "id")] = r; }
    std::vector<std::vector<uint8_t>> recs;
    for (auto& id : ids) {
        LeanRecord rec;
        const bool own = readLean(id, rec);
        if (!own && !readShared(id, rec)) continue;
        for (auto* k : kPrivateKeys) rec.meta.erase(std::remove_if(rec.meta.begin(), rec.meta.end(), [&](auto& kv) { return kv.first == k; }), rec.meta.end());
        if (names && own) { //your current values win over the file's tags, which win over what the record was built with
            auto r = rowOf.find(id); auto u = usr.find(id);
            auto value = [&](const char* k) {
                if (u != usr.end()) { auto v = u->second.find(k); if (v != u->second.end() && v->second.isNotEmpty()) return v->second; }
                if (r != rowOf.end()) { const juce::String x = idx.get(r->second, k); if (x.isNotEmpty()) return x; }
                return juce::String(rec.get(k));
            };
            for (const char* k : {"artist", "title", "album", "year", "genre"}) { const juce::String v = value(k); if (v.isNotEmpty()) rec.set(k, v.toStdString()); }
            const juce::String artist = value("artist"), title = value("title");
            juce::String name = title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " + title : title) : (r != rowOf.end() ? fileTitle(idx.get(r->second, "file")) : juce::String());
            if (name.isNotEmpty()) rec.set("name", name.toStdString());
        } else if (!names) rec.meta.erase(std::remove_if(rec.meta.begin(), rec.meta.end(), [](auto& kv) { return kv.first == "name"; }), rec.meta.end());
        if (!full) rec.replay.clear();
        recs.push_back(serialiseLean(rec));
    }
    rpPutVar(all, recs.size());
    for (auto& r : recs) { rpPutVar(all, r.size()); all.insert(all.end(), r.begin(), r.end()); }
    const auto packed = gzipBytes(all);
    if (!out.replaceWithData(packed.data(), packed.size())) return -1;
    return (int)recs.size();
}

juce::String Library::importPack(const juce::File& in, int* addedOut, std::vector<juce::String>* newIds, int* updatedOut)
{
    juce::FileInputStream fis(in);
    if (!fis.openedOk()) return "Could not open that file.";
    juce::GZIPDecompressorInputStream gz(&fis, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb; gz.readIntoMemoryBlock(mb);
    const uint8_t* p = (const uint8_t*)mb.getData(); const uint8_t* e = p + mb.getSize();
    if (mb.getSize() < 6 || p[0] != 'P' || p[1] != 'C' || p[2] != 'K' || p[3] != 1) return "That is not a meterDARK pack.";
    const bool full = p[4] == 1; p += 5;
    uint64_t n; if (!rpGetVar(p, e, n)) return "The pack is damaged.";
    const juce::File dir = folder.getChildFile("lean-shared");
    dir.createDirectory();
    int added = 0, updated = 0, own = 0, bad = 0, noName = 0;
    for (uint64_t i = 0; i < n; ++i) {
        uint64_t len; if (!rpGetVar(p, e, len) || (uint64_t)(e - p) < len) {++bad; break;}
        LeanRecord rec;
        if (!parseLean(std::vector<uint8_t>(p, p + len), rec)) {++bad; p += len; continue;}
        p += len;
        const juce::String id(rec.get("id"));
        if (id.isEmpty() || id.containsAnyOf("/\\:.")) {++bad; continue;}
        for (auto* k : kPrivateKeys) rec.meta.erase(std::remove_if(rec.meta.begin(), rec.meta.end(), [&](auto& kv) { return kv.first == k; }), rec.meta.end());
        if (leanFile(id).existsAsFile()) {++own; continue;} //you already measured this one yourself
        if (rec.get("name").empty() && rec.get("title").empty()) ++noName;
        const juce::File f = dir.getChildFile(id + ".lnr.gz");
        const bool had = f.existsAsFile();
        const auto bytes = gzipBytes(serialiseLean(rec));
        if (!f.replaceWithData(bytes.data(), bytes.size())) {++bad; continue;}
        if (had) ++updated; else { ++added; if (newIds) newIds->push_back(id); }
    }
    if (added + updated > 0) { ChangeNote cn; cn.kind = ChangeNote::SharedAll; notify(cn); }
    if (addedOut) *addedOut = added;
    if (updatedOut) *updatedOut = updated;
    juce::String msg = "Pack (" + juce::String(full ? "full" : "display") + "): " + juce::String(added) + " new, " + juce::String(updated) + " updated (already there)";
    if (own) msg << ", " << own << " skipped because you measured them yourself";
    if (bad) msg << ", " << bad << " could not be read";
    msg << ".";
    if (noName > 0) msg << " " << noName << " have no names (the pack was made without them, or by an older Analyser: export it again from the other machine).";
    return msg;
}

std::vector<juce::String> Library::sharedIds()
{
    std::vector<juce::String> out;
    for (auto& f : folder.getChildFile("lean-shared").findChildFiles(juce::File::findFiles, false, "*.lnr.gz")) out.push_back(f.getFileName().upToLastOccurrenceOf(".lnr.gz", false, false));
    std::sort(out.begin(), out.end());
    return out;
}

bool Library::readShared(const juce::String& id, LeanRecord& out)
{
    juce::FileInputStream fis(folder.getChildFile("lean-shared").getChildFile(id + ".lnr.gz"));
    if (!fis.openedOk()) return false;
    juce::GZIPDecompressorInputStream gz(&fis, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb; gz.readIntoMemoryBlock(mb);
    return parseLean(std::vector<uint8_t>((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize()), out);
}

bool Library::writeLean(const juce::String& id, const LeanRecord& rec)
{
    folder.getChildFile("lean").createDirectory();
    const auto packed = gzipBytes(serialiseLean(rec));
    const bool ok = leanFile(id).replaceWithData(packed.data(), packed.size());
    if (ok) { ChangeNote n; n.kind = ChangeNote::LeanOne; n.id = id; notify(n); }
    return ok;
}

bool Library::setLeanMeta(const juce::String& id, const std::vector<std::pair<std::string, std::string>>& changes)
{
    LeanRecord rec;
    if (!readLean(id, rec)) return false;
    bool differs = false;
    for (auto& kv : changes) { if (rec.get(kv.first) != kv.second) differs = true; rec.set(kv.first, kv.second); }
    return differs ? writeLean(id, rec) : true;
}

Library::LeanReport Library::buildLeanRecords(bool force, bool verify, std::function<bool()> shouldStop, std::function<void(int, int)> progress)
{
    LeanReport rep;
    const Table t = index();
    const auto users = userValues();
    const juce::String vid = versionIds().back();
    const MeterVersion* mv = findMeterVersion(vid.toStdString());
    folder.getChildFile("lean").createDirectory();
    const int total = (int)t.rows.size(); rep.totalTracks = total;
    if (mv == nullptr) return rep;
    std::atomic<int> next{0}, done{0};
    juce::CriticalSection m;
    auto worker = [&] {
        for (;;) {
            const int r = next++;
            if (r >= total || (shouldStop && shouldStop())) return;
            const juce::String id = t.get((size_t)r, "id");
            bool keep = false, nodata = false, mismatch = false; double bytes = 0, bytesReplay = 0;
            bool current = false;
            if (!force && leanFile(id).existsAsFile()) { LeanRecord old; current = readLean(id, old) && juce::String(old.get("meter_version")) == juce::String(mv->version); } //built with an older Meter: build again
            if (current) {
                keep = true; //already there: only your corrected tags and the audio source can have changed
                std::vector<std::pair<std::string, std::string>> ch;
                auto uv = users.find(id);
                for (const char* k : {"artist", "title", "album", "year"}) {
                    juce::String v = t.get((size_t)r, k);
                    if (uv != users.end()) { auto it = uv->second.find(k); if (it != uv->second.end() && it->second.isNotEmpty()) v = it->second; }
                    ch.emplace_back(k, v.toStdString());
                }
                juce::String src; if (uv != users.end()) { auto it = uv->second.find("source"); if (it != uv->second.end()) src = it->second; }
                ch.emplace_back("source", src.toStdString());
                setLeanMeta(id, ch);
            }
            else {
                std::vector<std::string> names; std::vector<float> values; double sr = 96000.0;
                if (!(readCoreAt(id, vid, 96000, names, values) && !names.empty())) { sr = t.get((size_t)r, "sample_rate").getDoubleValue(); if (sr <= 0) sr = 44100.0; if (!readCore(id, vid, names, values) || names.empty()) nodata = true; }
                if (!nodata) {
                    std::vector<std::pair<std::string, std::string>> meta;
                    auto add = [&](const char* k, const juce::String& v) { meta.emplace_back(k, v.toStdString()); };
                    add("id", id);
                    auto uv = users.find(id);
                    for (const char* k : {"artist", "title", "album", "year"}) { //your corrected tags win over the file's
                        juce::String v = t.get((size_t)r, k);
                        if (uv != users.end()) { auto it = uv->second.find(k); if (it != uv->second.end() && it->second.isNotEmpty()) v = it->second; }
                        add(k, v);
                    }
                    add("format", t.get((size_t)r, "format")); add("sample_rate", t.get((size_t)r, "sample_rate")); add("bits", t.get((size_t)r, "bits"));
                    add("length_s", t.get((size_t)r, "duration_s")); add("peak_db", t.get((size_t)r, "peak_dbfs")); add("true_peak_db", t.get((size_t)r, "true_peak_dbtp")); add("lufs", t.get((size_t)r, "lufs_integrated"));
                    add("lossy", t.get((size_t)r, "lossy"));
                    if (uv != users.end()) { auto it = uv->second.find("source"); if (it != uv->second.end() && it->second.isNotEmpty()) add("source", it->second); }
                    add("window_rate", juce::String(sr, 0));
                    LeanRecord rec = buildLeanRecord(*mv, sr, names, values, meta);
                    if (rec.columns == 0) nodata = true;
                    else {
                        const auto raw = serialiseLean(rec);
                        const auto packed = gzipBytes(raw);
                        bytes = (double)packed.size(); bytesReplay = (double)gzipBytes(rec.replay).size();
                        if (verify) { //the replay block must regrade to the stored grade
                            std::vector<std::string> n2; std::vector<float> c2;
                            auto run = mv->create(sr, 1);
                            if (unpackWindows(rec.replay, n2, c2) && !n2.empty()) { for (size_t w = 0; w < c2.size()/n2.size(); ++w) run->feedWindow(n2, &c2[w*n2.size()]); mismatch = run->result().grade != rec.get("grade"); }
                            else mismatch = true;
                            if (!mismatch) { // and against the exact data
                                auto exact = mv->create(sr, 1);
                                for (size_t w = 0; w < values.size()/names.size(); ++w) exact->feedWindow(names, &values[w*names.size()]);
                                mismatch = exact->result().grade != rec.get("grade") || exact->result().bonusValue != juce::String(rec.get("bonus_value")).getIntValue();
                            }
                        }
                        leanFile(id).replaceWithData(packed.data(), packed.size());
                        ChangeNote n; n.kind = ChangeNote::LeanOne; n.id = id; notify(n);
                    }
                }
            }
            { const juce::ScopedLock sl(m);
              if (keep) ++rep.kept; else if (nodata) ++rep.noData; else {++rep.built; rep.bytesAll += bytes; rep.bytesReplay += bytesReplay;}
              if (mismatch) ++rep.mismatches; }
            const int d = ++done; if (progress) progress(d, total);
        }
    };
    std::vector<std::thread> th;
    for (unsigned k = 0; k < juce::jmax(1u, std::thread::hardware_concurrency()); ++k) th.emplace_back(worker);
    for (auto& x : th) x.join();
    ++changes;
    return rep;
}

std::map<juce::String, double> Library::lowEndShares(std::function<bool()> shouldStop, std::function<void(int, int)> progress)
{
    std::map<juce::String, double> out;
    const juce::File cache = folder.getChildFile("lowend.csv");
    {
        juce::StringArray lines; lines.addLines(cache.loadFileAsString());
        for (auto& l : lines) {const int c = l.indexOfChar(','); if (c > 0 && l.substring(c + 1).getDoubleValue() >= 0) out[l.substring(0, c)] = l.substring(c + 1).getDoubleValue();} //-1 (no fine data then) is tried again
    }
    const Table t = index();
    std::vector<juce::String> todo;
    for (size_t r = 0; r < t.rows.size(); ++r) if (!out.count(t.get(r, "id"))) todo.push_back(t.get(r, "id"));
    if (todo.empty()) return out;
    std::atomic<int> next{0}, done{0};
    juce::CriticalSection m;
    const int total = (int)todo.size();
    const juce::File tracks = folder.getChildFile("tracks");
    auto worker = [&] {
        for (;;) {
            const int i = next++;
            if (i >= total || (shouldStop && shouldStop())) return;
            std::vector<float> fine; double low = 0, all = 0;
            if (readFloats(tracks.getChildFile(todo[(size_t)i] + ".fine.f32.gz"), fine) && fine.size() % F_NUM_COLS == 0) {
                for (size_t b = 0; b + F_NUM_COLS <= fine.size(); b += F_NUM_COLS)
                    for (int k = 0; k < 10; ++k) {const double v = fine[b + (size_t)F_BAND0 + (size_t)k]; all += v; if (k < 3) low += v;}
            }
            {const juce::ScopedLock sl(m); out[todo[(size_t)i]] = all > 0 ? low/all : -1.0;} //-1 = no fine data for this track
            const int d = ++done; if (progress) progress(d, total);
        }
    };
    std::vector<std::thread> th;
    for (unsigned k = 0; k < juce::jmax(1u, std::thread::hardware_concurrency()); ++k) th.emplace_back(worker);
    for (auto& x : th) x.join();
    juce::String csv;
    for (auto& kv : out) csv << kv.first << "," << juce::String(kv.second, 6) << "\n";
    cache.replaceWithText(csv);
    return out;
}

int Library::upgradeOldRows(std::function<bool()> shouldStop, std::function<void(int, int, const juce::String&)> progress)
{
    Table t = index();
    if (t.rows.empty()) return 0;
    //what each track still needs: grades from each Meter version (at the file's rate, and at the converted anchor rates where the newest
    //version's windows are stored), and the BPM estimate
    const juce::String newestSrc = windowSource(versionIds().back());
    struct Need { juce::String vid; int rate; };
    struct Job { size_t row; std::vector<Need> missing; bool bpm; };
    std::vector<Job> jobs;
    std::map<juce::String, std::map<int, std::set<juce::String>>> have; // version -> rate -> ids with a grade row
    for (auto& v : versionIds()) {
        { const Table g = gradeFile(v, 0); for (size_t r = 0; r < g.rows.size(); ++r) have[v][0].insert(g.get(r, "id")); }
        if (windowSource(v) == newestSrc) for (int a : anchorRates()) { const Table g = gradeFile(v, a); for (size_t r = 0; r < g.rows.size(); ++r) have[v][a].insert(g.get(r, "id")); }
    }
    for (size_t r = 0; r < t.rows.size(); ++r) {
        const juce::String id = t.get(r, "id");
        Job j{r, {}, t.get(r, "bpm_confidence").isEmpty()};
        const int fileRate = juce::roundToInt(t.get(r, "sample_rate").getDoubleValue());
        for (auto& v : versionIds()) {
            if (have[v][0].count(id) == 0) j.missing.push_back({v, 0});
            if (windowSource(v) == newestSrc) for (int a : anchorRates()) //only where those windows were stored (tracks scanned before the anchor rates need a rescan for them)
                if (a != fileRate && have[v][a].count(id) == 0 && (rateCoreFile(id, v, a).existsAsFile() || rateCoreFile(id, windowSource(v), a).existsAsFile())) j.missing.push_back({v, a});
        }
        if (j.bpm || !j.missing.empty()) jobs.push_back(j);
    }
    if (jobs.empty()) return 0;
    //grade rows are collected and written a few hundred at a time (each grades file is read and written once per batch, not once per track)
    juce::CriticalSection pendLock; std::map<std::pair<juce::String, int>, std::vector<std::pair<juce::String, OriginalResult>>> pending; size_t pendingCount = 0;
    auto flush = [&](bool all) {
        decltype(pending) out;
        { const juce::ScopedLock pl(pendLock); if (!all && pendingCount < 300) return; out.swap(pending); pendingCount = 0; }
        for (auto& kv : out) putGradeRows(kv.first.first, kv.first.second, kv.second);
    };
    std::atomic<int> next{0}, done{0}, changed{0}, noAudio{0};
    auto work = [&] {
        for (int k; (k = next++) < (int)jobs.size();) {
            if (shouldStop && shouldStop()) return;
            const Job& j = jobs[(size_t)k];
            const juce::String id = t.get(j.row, "id");
            const juce::File tracks = folder.getChildFile("tracks");
            bool any = false;
            std::vector<juce::String> needAudio;
            for (auto& need : j.missing) { //replay the stored window data where it exists (no audio needed); a version that measures like an older one uses that one's windows
                std::vector<std::string> names; std::vector<float> values;
                const bool got = need.rate == 0 ? readCore(id, need.vid, names, values) : readCoreAt(id, need.vid, need.rate, names, values);
                if (got && !names.empty()) {
                    const double sr = need.rate > 0 ? (double)need.rate : t.get(j.row, "sample_rate").getDoubleValue();
                    auto run = findMeterVersion(need.vid.toStdString())->create(sr, 1);
                    for (size_t w = 0; w + names.size() <= values.size(); w += names.size()) run->feedWindow(names, &values[w]);
                    { const juce::ScopedLock pl(pendLock); pending[{need.vid, need.rate}].push_back({id, run->result()}); ++pendingCount; }
                    any = true;
                } else if (need.rate == 0) needAudio.push_back(need.vid); //no windows at the anchor rates: those stay as they are until a rescan
            }
            flush(false);
            if (!needAudio.empty()) { //a Meter version that measures differently: it needs the audio again
                const juce::File src(t.get(j.row, "source_path"));
                if (src.existsAsFile() && scanVersions(id, src, needAudio, {}, shouldStop)) any = true;
                else ++noAudio;
            }
    if (j.bpm) {
                std::vector<float> fine;
                const juce::var meta = juce::JSON::parse(tracks.getChildFile(id + ".meta.json"));
                const double sr = meta.getProperty("sample_rate", 0.0), blk = meta.getProperty("fine_block_samples", 0.0);
                if (sr > 0 && blk > 0 && readFloats(tracks.getChildFile(id + ".fine.f32.gz"), fine) && fine.size() % F_NUM_COLS == 0) {
                    const auto b = estimateBpm(fine, blk/sr);
                    const juce::ScopedLock sl(lock);
                    Table cur; cur.load(indexFile());
                    const int r = cur.findRow("id", id);
                    if (r >= 0) {cur.set((size_t)r, "bpm_est", b.bpm > 0 ? num(b.bpm, 1) : juce::String()); cur.set((size_t)r, "bpm_confidence", num(b.confidence, 2)); cur.save(indexFile());
                        ChangeNote n; n.kind = ChangeNote::IndexRow; n.id = id; n.header = cur.header; n.cells = cur.rows[(size_t)r]; notify(n);}
                    any = true;
                }
            }
            if (any) ++changed;
            const int d = ++done;
            if (progress) progress(d, (int)jobs.size(), t.get(j.row, "file"));
        }
    };
    const int threads = juce::jlimit(1, 8, (int)std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) pool.emplace_back(work);
    work();
    for (auto& th : pool) th.join();
    flush(true);
    lastNoAudio = noAudio.load();
    {   //an index from before the grades moved to their own files: rewrite it in the current shape
        const juce::ScopedLock sl(lock);
        Table cur; cur.load(indexFile());
        juce::StringArray want; for (auto c : kIndexHeader) want.add(c);
        if (cur.header != want) {Table n; n.header = want; for (size_t r = 0; r < cur.rows.size(); ++r) {n.rows.push_back({}); for (auto& c : want) n.rows.back().add(cur.get(r, c));} n.save(indexFile()); ChangeNote cn; cn.kind = ChangeNote::ReloadAll; notify(cn);}
    }
    return changed.load();
}

int Library::tracksNeedingRescan()
{
    const Table t = index();
    int n = 0;
    for (size_t r = 0; r < t.rows.size(); ++r) if (t.get(r, "capture_version").getDoubleValue() < juce::String(kCaptureVersion).getDoubleValue()) ++n;
    return n;
}

int Library::rescanLibrary(std::function<bool()> shouldStop, std::function<void(int, int, const juce::String&)> progress, std::function<void(const juce::String&)> log)
{
    const Table t = index();
    struct Job { juce::String id, file, path; };
    std::vector<Job> jobs;
    const double want = juce::String(kCaptureVersion).getDoubleValue();
    for (size_t r = 0; r < t.rows.size(); ++r)
        if (t.get(r, "capture_version").getDoubleValue() < want) jobs.push_back({t.get(r, "id"), t.get(r, "file"), t.get(r, "source_path")});
    if (jobs.empty()) return 0;
    std::atomic<int> next{0}, done{0}, rescanned{0}, missing{0};
    juce::CriticalSection logLock;
    auto say = [&](const juce::String& m) { if (log) {const juce::ScopedLock sl(logLock); log(m);} };
    auto work = [&] {
        for (int k; (k = next++) < (int)jobs.size();) {
            if (shouldStop && shouldStop()) return;
            const Job& j = jobs[(size_t)k];
            const juce::File src(j.path);
            if (j.path.isEmpty() || !src.existsAsFile()) {++missing; say("not found, left as it is: " + (j.path.isEmpty() ? j.file : j.path));}
            else if (juce::SHA256(src).toHexString().substring(0, 16) != j.id) {++missing; say("the file at this path is not the one that was scanned, left as it is: " + j.path);}
            else {
                const AnalyseOutcome o = analyse(src, {}, shouldStop);
                if (o.status == AnalyseOutcome::Updated || o.status == AnalyseOutcome::AlreadyThere) ++rescanned;
                else if (o.message != "stopped") say("could not rescan " + j.file + ": " + o.message);
            }
            const int d = ++done;
            if (progress) progress(d, (int)jobs.size(), j.file);
        }
    };
    const int threads = juce::jlimit(1, 8, (int)std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) pool.emplace_back(work);
    work();
    for (auto& th : pool) th.join();
    lastNoAudio = missing.load();
    return rescanned.load();
}

void Library::writeReadme()
{
    const juce::File f = folder.getChildFile("README.txt");
    f.replaceWithText(
        "METERDARK LIBRARY\r\n"
        "=================\r\n"
        "Made by meterDARK Analyser. Each track you add is captured once and kept here. Nothing is ever interpreted on the way in:\r\n"
        "every gauge, grade or hint can be worked out again later from these files, without the audio.\r\n\r\n"
        "index.csv              one row per track: tags, format, levels, loudness, BPM and key estimates, and where the file was.\r\n"
        "grades-<version>.csv   what each ORIGINAL Airwindows Meter version reads for every track, with the audio as the file has it.\r\n"
        "grades-<version>@44100.csv, @96000.csv   the same, after the audio was converted to that sample rate first (a high-quality\r\n"
        "                       resampler, the same one meterDARK uses). The sample rate of the file changes what Meter reads, so the rate\r\n"
        "                       the Library and the Lab always show the 96 kHz ones. A track already at that rate has no row in these files: its own grades are used.\r\n"
        "filters.json   your Library filters and saved filter sets.\r\n"
        "tracks/<id>.core-<version>.csv   the original meter's own data: one row per audio window (1882 samples at 44.1 kHz, about 42.7 ms),\r\n"
        "                       the values the plugin sends to its display (peak is square-rooted, as in the plugin). Column names are in the header.\r\n"
        "tracks/<id>.core-<newest version>@44100.f32.gz, @96000.f32.gz   the same window data of the newest Meter version for the converted audio:\r\n"
        "                       gzip-compressed little-endian float32, one row per window; the column names are in <id>.meta.json (rate_runs).\r\n"
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

} // namespace mdark
