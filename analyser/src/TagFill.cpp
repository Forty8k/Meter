#include "TagFill.h"
#include <set>
#include <map>
#include <thread>
#include <chrono>

namespace mdark { namespace tagfill {

static juce::String lettersOnly(const juce::String& s) { juce::String o; for (auto c : s) if (juce::CharacterFunctions::isLetterOrDigit(c)) o << juce::CharacterFunctions::toLowerCase(c); return o; }

juce::String genreFromFolderName(const juce::String& folderName)
{
    static const std::map<juce::String, juce::String> k = {
        {"rock", "Rock"}, {"pop", "Pop"}, {"jazz", "Jazz"}, {"hiphop", "Hip Hop"}, {"rap", "Hip Hop"}, {"electronic", "Electronic"}, {"electronica", "Electronic"}, {"dance", "Dance"},
        {"house", "House"}, {"techno", "Techno"}, {"drumandbass", "Drum & Bass"}, {"drumbass", "Drum & Bass"}, {"drumnbass", "Drum & Bass"}, {"dnb", "Drum & Bass"}, {"dubstep", "Dubstep"}, {"reggae", "Reggae"},
        {"soul", "Soul"}, {"funk", "Funk"}, {"rnb", "R&B"}, {"randb", "R&B"}, {"blues", "Blues"}, {"country", "Country"}, {"folk", "Folk"}, {"metal", "Metal"}, {"punk", "Punk"},
        {"indie", "Indie"}, {"classical", "Classical"}, {"soundtrack", "Soundtrack"}, {"ost", "Soundtrack"}, {"disco", "Disco"}, {"ambient", "Ambient"}, {"latin", "Latin"},
        {"world", "World"}, {"gospel", "Gospel"}, {"alternative", "Alternative"}, {"hardrock", "Hard Rock"}, {"classicrock", "Classic Rock"}, {"newwave", "New Wave"},
        {"trance", "Trance"}, {"garage", "Garage"}, {"grime", "Grime"}, {"ska", "Ska"}, {"easylistening", "Easy Listening"}, {"singersongwriter", "Singer-Songwriter"}, {"christmas", "Christmas"}};
    auto it = k.find(lettersOnly(folderName));
    return it == k.end() ? juce::String() : it->second;
}

static bool looksLikeYear(const juce::String& s) { if (s.length() != 4 || !s.containsOnly("0123456789")) return false; const int y = s.getIntValue(); return y >= 1900 && y <= 2035; }

static juce::String yearInName(const juce::String& name)
{   // "(1997)", "[1997]", "1997 - Album", "Album - 1997"
    for (int i = 0; i + 4 <= name.length(); ++i) {
        const juce::String cand = name.substring(i, i + 4);
        if (!looksLikeYear(cand)) continue;
        const bool before = i == 0 || !juce::CharacterFunctions::isDigit(name[i - 1]);
        const bool after = i + 4 >= name.length() || !juce::CharacterFunctions::isDigit(name[i + 4]);
        if (before && after) return cand;
    }
    return {};
}

static juce::String stripYearDecor(juce::String name)
{   // "1997 - Album" / "Album (1997)" -> "Album"
    const juce::String y = yearInName(name);
    if (y.isEmpty()) return name.trim();
    name = name.replace("(" + y + ")", "").replace("[" + y + "]", "");
    if (name.startsWith(y)) name = name.substring(4).trimCharactersAtStart(" -_.");
    else if (name.endsWith(y)) name = name.dropLastCharacters(4).trimCharactersAtEnd(" -_.");
    return name.trim();
}

Facts fromPath(const juce::String& fullPath)
{
    Facts f;
    juce::StringArray parts; parts.addTokens(fullPath.replaceCharacter('\\', '/'), "/", "");
    parts.removeEmptyStrings();
    if (parts.size() < 2) return f;
    parts.remove(parts.size() - 1); // the file name
    // the nearest genre-named folder above the file wins
    int genreAt = -1;
    for (int i = parts.size() - 1; i >= 0; --i) { const auto g = genreFromFolderName(parts[i]); if (g.isNotEmpty()) { f.genre = g; genreAt = i; break; } }
    // Artist/Album/track: the last folder is the album when the one above it is not a genre name and is not a drive/root-like name
    const int n = parts.size();
    // a year in the album folder's name is a safe hint on its own
    const juce::String y = yearInName(parts[n - 1]);
    if (y.isNotEmpty()) f.year = y;
    // artist / album only where a genre folder shows the collection is organised that way: Genre/Artist/Album/track or Genre/Artist/track
    auto usable = [&](int i) { return i >= 0 && i != genreAt && genreFromFolderName(parts[i]).isEmpty() && parts[i].length() > 1 && !looksLikeYear(parts[i]); };
    if (genreAt >= 0) {
        if (genreAt == n - 3 && usable(n - 2) && usable(n - 1)) { f.artist = parts[n - 2].trim(); f.album = stripYearDecor(parts[n - 1]); }
        else if (genreAt == n - 2 && usable(n - 1)) f.artist = stripYearDecor(parts[n - 1]);
        else if (genreAt == n - 1) {}
    }
    return f;
}

juce::String cleanTitle(const juce::String& title)
{
    juce::String t = title;
    // drop bracketed remaster / feat / version notes, and " - 2015 Remaster" style tails
    for (;;) {
        const int a = t.lastIndexOfChar('('), b = t.lastIndexOfChar(')');
        if (a >= 0 && b > a && b == t.length() - 1) {
            const juce::String inner = t.substring(a + 1, b).toLowerCase();
            if (inner.contains("remaster") || inner.contains("feat") || inner.contains("ft.") || inner.contains("live") || inner.contains("version") || inner.contains("mix")
                || inner.contains("edit") || inner.contains("mono") || inner.contains("stereo") || inner.contains("deluxe") || inner.contains("bonus") || looksLikeYear(inner.trim())) { t = t.substring(0, a).trim(); continue; }
        }
        break;
    }
    const int dash = t.lastIndexOf(" - ");
    if (dash > 0) { const juce::String tail = t.substring(dash + 3).toLowerCase(); if (tail.contains("remaster") || tail.contains("mono") || tail.contains("single version") || tail.contains("radio edit")) t = t.substring(0, dash).trim(); }
    return t.trim();
}

static juce::String yearOf(const juce::String& date) { const juce::String y = date.substring(0, 4); return looksLikeYear(y) ? y : juce::String(); }

Found parseMusicBrainz(const juce::String& jsonText)
{
    Found out;
    const juce::var v = juce::JSON::parse(jsonText);
    auto* recs = v.getProperty("recordings", juce::var()).getArray();
    if (recs == nullptr) return out;
    juce::String bestYear; int bestCount = -1; juce::String bestGenre;
    for (auto& r : *recs) {
        if ((int)r.getProperty("score", 0) < 90) continue;
        juce::String y = yearOf(r.getProperty("first-release-date", juce::var()).toString());
        if (auto* rel = r.getProperty("releases", juce::var()).getArray())
            for (auto& x : *rel) { const juce::String d = yearOf(x.getProperty("date", juce::var()).toString()); if (d.isNotEmpty() && (y.isEmpty() || d < y)) y = d; }
        if (y.isNotEmpty() && (bestYear.isEmpty() || y < bestYear)) bestYear = y;
        for (const char* key : {"genres", "tags"}) {
            if (auto* gl = r.getProperty(key, juce::var()).getArray())
                for (auto& g : *gl) { const int c = (int)g.getProperty("count", 0); if (c > bestCount) { bestCount = c; bestGenre = g.getProperty("name", juce::var()).toString(); } }
            if (bestGenre.isNotEmpty()) break;
        }
    }
    if (bestGenre.isNotEmpty()) bestGenre = bestGenre.substring(0, 1).toUpperCase() + bestGenre.substring(1);
    out.year = bestYear; out.genre = bestGenre; out.ok = bestYear.isNotEmpty() || bestGenre.isNotEmpty();
    return out;
}

Found parseDiscogs(const juce::String& jsonText)
{
    Found out;
    const juce::var v = juce::JSON::parse(jsonText);
    auto* res = v.getProperty("results", juce::var()).getArray();
    if (res == nullptr) return out;
    for (auto& r : *res) {
        const juce::String y = r.getProperty("year", juce::var()).toString().trim();
        if (looksLikeYear(y) && (out.year.isEmpty() || y < out.year)) out.year = y;
        if (out.genre.isEmpty()) if (auto* g = r.getProperty("genre", juce::var()).getArray()) if (g->size() > 0) out.genre = (*g)[0].toString();
    }
    out.ok = out.year.isNotEmpty() || out.genre.isNotEmpty();
    return out;
}

juce::String musicBrainzUrl(const juce::String& artist, const juce::String& title)
{
    const juce::String q = "recording:\"" + title.replace("\"", " ") + "\" AND artist:\"" + artist.replace("\"", " ") + "\"";
    return "https://musicbrainz.org/ws/2/recording/?fmt=json&limit=5&query=" + juce::URL::addEscapeChars(q, true);
}
juce::String discogsUrl(const juce::String& artist, const juce::String& title, const juce::String& token)
{
    return "https://api.discogs.com/database/search?type=release&per_page=10&artist=" + juce::URL::addEscapeChars(artist, true) + "&track=" + juce::URL::addEscapeChars(title, true)
           + "&token=" + juce::URL::addEscapeChars(token, true);
}

static const char* kUserAgent = "meterDARKAnalyser/1.1 ( https://github.com/Forty8k/Meter )";

static juce::String fetch(const juce::String& url, bool& failed)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        int status = 0;
        auto stream = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                           .withConnectionTimeoutMs(15000).withExtraHeaders(juce::String("User-Agent: ") + kUserAgent + "\r\nAccept: application/json\r\n").withStatusCode(&status));
        if (stream != nullptr && status == 200) { failed = false; return stream->readEntireStreamAsString(); }
        failed = true;
        if (status == 503 || status == 429 || stream == nullptr) std::this_thread::sleep_for(std::chrono::seconds(3)); else break;
    }
    return {};
}

static juce::File logFile(Library& lib) { return lib.folder.getChildFile("tagfill-log.csv"); }
static juce::File cacheFile(Library& lib) { return lib.folder.getChildFile("tagfill-cache.json"); }

Summary fill(Library& lib, const Options& o, std::function<void(int, int, const juce::String&)> progress, std::function<bool()> shouldStop)
{
    Summary sum;
    const Table idx = lib.index();
    auto users = lib.userValues();
    auto have = [&](const juce::String& id, size_t row, const char* key) {
        auto u = users.find(id);
        if (u != users.end()) { auto v = u->second.find(key); if (v != u->second.end() && v->second.isNotEmpty()) return true; }
        return idx.get(row, key).trim().isNotEmpty();
    };
    std::vector<Library::UserEdit> pending; juce::String logText;
    auto remember = [&](const juce::String& id, const juce::String& key, const juce::String& val, const char* src) {
        pending.push_back({id, key, val}); users[id][key] = val;
        logText << id << "," << key << ",\"" << val.replace("\"", "'") << "\"," << src << "," << juce::Time::getCurrentTime().toISO8601(true) << "\n";
    };
    auto flush = [&]() {
        if (pending.empty()) return;
        lib.setUserValues(pending); pending.clear();
        const auto f = logFile(lib);
        if (!f.existsAsFile()) f.replaceWithText("id,field,value,source,time\n");
        f.appendText(logText); logText.clear();
    };
    sum.tracks = (int)idx.rows.size();
    // 1. folders
    if (o.folders) {
        for (size_t r = 0; r < idx.rows.size(); ++r) {
            if (shouldStop && shouldStop()) break;
            const juce::String id = idx.get(r, "id");
            const auto f = fromPath(idx.get(r, "source_path"));
            if (f.genre.isNotEmpty() && !have(id, r, "genre")) { remember(id, "genre", f.genre, "folder"); ++sum.genres; }
            if (f.year.isNotEmpty() && !have(id, r, "year")) { remember(id, "year", f.year, "folder"); ++sum.years; }
            if (f.artist.isNotEmpty() && !have(id, r, "artist")) { remember(id, "artist", f.artist, "folder"); ++sum.artists; }
            if (f.album.isNotEmpty() && !have(id, r, "album")) { remember(id, "album", f.album, "folder"); ++sum.albums; }
            if (progress && r % 200 == 0) progress((int)r, (int)idx.rows.size(), "Reading folder names ...");
        }
        flush();
    }
    // 2. online
    if ((o.musicBrainz || (o.discogs && o.discogsToken.isNotEmpty())) && !(shouldStop && shouldStop())) {
        juce::var cacheVar = juce::JSON::parse(cacheFile(lib));
        auto* cache = cacheVar.isObject() ? cacheVar.getDynamicObject() : new juce::DynamicObject();
        if (!cacheVar.isObject()) cacheVar = juce::var(cache);
        std::vector<size_t> todo;
        for (size_t r = 0; r < idx.rows.size(); ++r) {
            const juce::String id = idx.get(r, "id");
            auto u = users[id];
            const juce::String artist = !u["artist"].isEmpty() ? u["artist"] : idx.get(r, "artist");
            const juce::String title = !u["title"].isEmpty() ? u["title"] : idx.get(r, "title");
            if (artist.trim().isEmpty() || title.trim().isEmpty()) continue;
            if (!have(id, r, "year") || !have(id, r, "genre")) todo.push_back(r);
        }
        int done = 0; const int total = (int)todo.size();
        for (size_t r : todo) {
            if (shouldStop && shouldStop()) break;
            const juce::String id = idx.get(r, "id");
            auto u = users[id];
            const juce::String artist = (!u["artist"].isEmpty() ? u["artist"] : idx.get(r, "artist")).trim();
            const juce::String title = cleanTitle(!u["title"].isEmpty() ? u["title"] : idx.get(r, "title"));
            const juce::String key = artist.toLowerCase() + "|" + title.toLowerCase();
            if (progress) progress(done, total, "Looking up: " + artist + " - " + title);
            auto lookup = [&](const char* src, const juce::String& url, Found (*parse)(const juce::String&)) {
                const juce::String ck = juce::String(src) + "|" + key;
                juce::String body;
                if (cache->hasProperty(ck)) body = cache->getProperty(ck).toString();
                else {
                    bool failed = false; body = fetch(url, failed); ++sum.lookups;
                    if (failed) { ++sum.failures; std::this_thread::sleep_for(std::chrono::milliseconds(1100)); return Found(); }
                    cache->setProperty(ck, body);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // MusicBrainz and Discogs both ask for at most about one request a second
                }
                return parse(body);
            };
            Found mb, dc;
            if (o.musicBrainz) mb = lookup("mb", musicBrainzUrl(artist, title), parseMusicBrainz);
            if (o.discogs && o.discogsToken.isNotEmpty() && (!have(id, r, "genre") || !have(id, r, "year"))) dc = lookup("dc", discogsUrl(artist, title, o.discogsToken), parseDiscogs);
            // Discogs genre first, then MusicBrainz; the earliest year of the two (a folder value already filled wins because it is no longer empty)
            if (!have(id, r, "genre")) { const juce::String g = dc.genre.isNotEmpty() ? dc.genre : mb.genre; if (g.isNotEmpty()) { remember(id, "genre", g, dc.genre.isNotEmpty() ? "discogs" : "musicbrainz"); ++sum.genres; } }
            if (!have(id, r, "year")) {
                juce::String y = mb.year; if (dc.year.isNotEmpty() && (y.isEmpty() || dc.year < y)) y = dc.year;
                if (y.isNotEmpty()) { remember(id, "year", y, y == mb.year ? "musicbrainz" : "discogs"); ++sum.years; }
            }
            ++done;
            if (done % 25 == 0) { flush(); cacheFile(lib).replaceWithText(juce::JSON::toString(cacheVar)); }
        }
        flush(); cacheFile(lib).replaceWithText(juce::JSON::toString(cacheVar));
    }
    return sum;
}

int undoLast(Library& lib)
{
    const auto f = logFile(lib);
    if (!f.existsAsFile()) return 0;
    juce::StringArray lines; lines.addLines(f.loadFileAsString());
    auto users = lib.userValues();
    std::vector<Library::UserEdit> remove; int n = 0;
    for (int i = 1; i < lines.size(); ++i) {
        const auto t = Table::parse(lines[i]);
        if (t.empty() || t[0].size() < 3) continue;
        const juce::String id = t[0][0], key = t[0][1], val = t[0][2];
        auto u = users.find(id);
        if (u != users.end() && u->second[key] == val) { remove.push_back({id, key, {}}); ++n; }
    }
    lib.setUserValues(remove);
    f.deleteFile();
    return n;
}

}} // namespace
