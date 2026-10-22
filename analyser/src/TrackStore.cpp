#include "TrackStore.h"
#include "ReplayPack.h"
#include <thread>

namespace mdark {

static const size_t kLogMax = 20000;

TrackStore::TrackStore(std::shared_ptr<Library> library) : lib(std::move(library))
{
    loadAll();                                                  // nothing is writing yet (see the header), so nothing can be missed
    lib->onChange = [this](const ChangeNote& n) { apply(n); };
}

TrackStore::~TrackStore() { if (lib) lib->onChange = nullptr; }

//---------------------------------------------------------------- filling it, once
void TrackStore::loadAll()
{
    Table idx = lib->index();
    auto usr = lib->userValues();
    auto cols = lib->customColumns();
    std::map<juce::String, GradeSet> gs;
    for (auto& v : Library::versionIds()) {
        GradeSet& g = gs[v];
        const Table nat = lib->gradeFile(v, 0), r96 = lib->gradeFile(v, 96000);
        g.header = nat.header.isEmpty() ? r96.header : nat.header;
        for (size_t r = 0; r < nat.rows.size(); ++r) g.nat[nat.get(r, "id")] = nat.rows[r];
        for (size_t r = 0; r < r96.rows.size(); ++r) g.r96[r96.get(r, "id")] = r96.rows[r];
    }
    const juce::ScopedLock sl(lock);
    indexT = std::move(idx); rebuildRowOf();
    user = std::move(usr); customs = std::move(cols); grade = std::move(gs);
    leanKept.clear(); sharedKept.clear(); sharedListKnown = false; ++epoch;
}

void TrackStore::rebuildRowOf()
{
    rowOf.clear();
    for (size_t r = 0; r < indexT.rows.size(); ++r) rowOf[indexT.get(r, "id")] = r;
}

//---------------------------------------------------------------- the log
void TrackStore::log(StoreChange::Kind k, const juce::String& id)
{
    // called with `lock` held
    changeLog.push_back({k, id});
    if (changeLog.size() > kLogMax) { changeLog.pop_front(); ++logStart; }
    ++seq_;
}

bool TrackStore::changesSince(uint64_t since, std::vector<StoreChange>& out, uint64_t& now) const
{
    const juce::ScopedLock sl(lock);
    out.clear();
    now = seq_.load();
    if (since > now) return false;
    if (since < logStart) return false;
    for (uint64_t i = since; i < now; ++i) out.push_back(changeLog[(size_t)(i - logStart)]);
    return true;
}

//---------------------------------------------------------------- one change at a time
void TrackStore::apply(const ChangeNote& n)
{
    using K = ChangeNote::Kind;
    // anything that needs reading from the Library is read BEFORE taking our lock (the Library's lock is already held by this thread, so this cannot wait on another thread)
    std::vector<CustomColumn> newCols; Table newIndex; std::map<juce::String, std::map<juce::String, juce::String>> newUser;
    if (n.kind == K::Columns) newCols = lib->customColumns();
    if (n.kind == K::ReloadUser) { newUser = lib->userValues(); newCols = lib->customColumns(); }
    if (n.kind == K::ReloadAll) { loadAll(); const juce::ScopedLock sl(lock); { const juce::ScopedLock wl(windowLock); windowKept.clear(); ++epoch; } log(StoreChange::Reset, {}); return; }

    const juce::ScopedLock sl(lock);
    switch (n.kind) {
        case K::IndexRow: {
            if (indexT.header != n.header) { //an index from an older analyser, carried over to the current columns
                Table t; t.header = n.header;
                for (size_t r = 0; r < indexT.rows.size(); ++r) { t.rows.push_back({}); for (auto& c : n.header) t.rows.back().add(indexT.get(r, c)); }
                indexT = std::move(t); rebuildRowOf();
            }
            auto it = rowOf.find(n.id);
            if (it != rowOf.end()) { indexT.rows[it->second] = n.cells; log(StoreChange::Regraded, n.id); }
            else { indexT.rows.push_back(n.cells); rowOf[n.id] = indexT.rows.size() - 1; log(StoreChange::Added, n.id); }
            break;
        }
        case K::GradeRow: {
            GradeSet& g = grade[n.version];
            if (g.header.isEmpty() || g.header != n.header) g.header = n.header;
            if (n.rate == 0) g.nat[n.id] = n.cells;
            else if (n.rate == 96000) g.r96[n.id] = n.cells;
            else break;                                          // 44.1 kHz readings are stored for later; no tab shows them
            { const juce::ScopedLock wl(windowLock); for (auto& kv : windowKept) kv.second.erase(n.id); ++winEpoch[n.id]; }   // its window data is being rewritten as well
            log(StoreChange::Regraded, n.id);
            break;
        }
        case K::UserValue: {
            if (n.value.isEmpty()) { auto u = user.find(n.id); if (u != user.end()) { u->second.erase(n.key); if (u->second.empty()) user.erase(u); } }
            else user[n.id][n.key] = n.value;
            log(StoreChange::Values, n.id);
            break;
        }
        case K::Removed: {
            auto it = rowOf.find(n.id);
            if (it != rowOf.end()) { indexT.rows.erase(indexT.rows.begin() + (long)it->second); rebuildRowOf(); }
            user.erase(n.id);
            for (auto& kv : grade) { kv.second.nat.erase(n.id); kv.second.r96.erase(n.id); }
            leanKept.erase(n.id); sharedKept.erase(n.id); ++dataEpoch[n.id];
            { const juce::ScopedLock wl(windowLock); for (auto& kv : windowKept) kv.second.erase(n.id); ++winEpoch[n.id]; }
            log(StoreChange::Removed, n.id);
            break;
        }
        case K::Columns: customs = std::move(newCols); log(StoreChange::Columns, {}); break;
        case K::ReloadUser: user = std::move(newUser); customs = std::move(newCols); log(StoreChange::Values, {}); log(StoreChange::Columns, {}); break;
        case K::LeanOne: {
            leanKept.erase(n.id); ++dataEpoch[n.id];
            { const juce::ScopedLock wl(windowLock); for (auto& kv : windowKept) if (kv.first.endsWith("|b")) kv.second.erase(n.id); ++winEpoch[n.id]; }
            log(StoreChange::Lean, n.id);
            break;
        }
        case K::SharedAll: sharedKept.clear(); sharedListKnown = false; ++epoch;
            { const juce::ScopedLock wl(windowLock); for (auto& kv : windowKept) if (kv.first.endsWith("|b")) for (auto it = kv.second.begin(); it != kv.second.end();) { if (!rowOf.count(it->first)) it = kv.second.erase(it); else ++it; } } //shared records' replay blocks may be new
            log(StoreChange::Shared, {}); break;
        case K::ReloadAll: break;
    }
}

//---------------------------------------------------------------- reading it
Table TrackStore::index() const { const juce::ScopedLock sl(lock); return indexT; }
int TrackStore::trackCount() const { const juce::ScopedLock sl(lock); return (int)indexT.rows.size(); }
bool TrackStore::indexRow(const juce::String& id, juce::StringArray& row, juce::StringArray* header) const
{
    const juce::ScopedLock sl(lock);
    auto it = rowOf.find(id); if (it == rowOf.end()) return false;
    row = indexT.rows[it->second]; if (header) *header = indexT.header;
    return true;
}
std::map<juce::String, std::map<juce::String, juce::String>> TrackStore::userValues() const { const juce::ScopedLock sl(lock); return user; }
std::map<juce::String, juce::String> TrackStore::userValuesOf(const juce::String& id) const { const juce::ScopedLock sl(lock); auto u = user.find(id); return u == user.end() ? std::map<juce::String, juce::String>() : u->second; }
std::vector<CustomColumn> TrackStore::customColumns() const { const juce::ScopedLock sl(lock); return customs; }

Table TrackStore::nativeGrades(const juce::String& v) const
{
    const juce::ScopedLock sl(lock);
    Table t; auto g = grade.find(v); if (g == grade.end()) return t;
    t.header = g->second.header;
    for (auto& kv : g->second.nat) t.rows.push_back(kv.second);
    return t;
}
Table TrackStore::readings(const juce::String& v) const
{
    const juce::ScopedLock sl(lock);
    Table t; auto g = grade.find(v); if (g == grade.end()) return t;
    t.header = g->second.header;
    for (auto& kv : g->second.r96) t.rows.push_back(kv.second);
    for (auto& kv : g->second.nat) if (!g->second.r96.count(kv.first)) t.rows.push_back(kv.second);
    return t;
}

std::shared_ptr<const LeanRecord> TrackStore::lean(const juce::String& id)
{
    uint64_t e;
    { const juce::ScopedLock sl(lock); auto it = leanKept.find(id); if (it != leanKept.end()) return it->second; e = dataStamp(id); }
    auto rec = std::make_shared<LeanRecord>();
    std::shared_ptr<const LeanRecord> out = lib->readLean(id, *rec) ? rec : nullptr;
    const juce::ScopedLock sl(lock);
    if (dataStamp(id) != e) return out;                                          // something changed while reading: use it, but do not keep it
    auto it = leanKept.find(id); if (it != leanKept.end()) return it->second;   // somebody was faster
    leanKept[id] = out;
    return out;
}
std::vector<juce::String> TrackStore::sharedIds()
{
    uint64_t e;
    { const juce::ScopedLock sl(lock); if (sharedListKnown) return sharedList; e = epoch.load(); }
    auto l = lib->sharedIds();
    const juce::ScopedLock sl(lock);
    if (epoch.load() != e) return l;
    sharedList = l; sharedListKnown = true;
    return sharedList;
}
std::shared_ptr<const LeanRecord> TrackStore::shared(const juce::String& id)
{
    uint64_t e;
    { const juce::ScopedLock sl(lock); auto it = sharedKept.find(id); if (it != sharedKept.end()) return it->second; e = dataStamp(id); }
    auto rec = std::make_shared<LeanRecord>();
    std::shared_ptr<const LeanRecord> out = lib->readShared(id, *rec) ? rec : nullptr;
    const juce::ScopedLock sl(lock);
    if (dataStamp(id) != e) return out;
    sharedKept[id] = out;
    return out;
}

//---------------------------------------------------------------- window data
std::shared_ptr<const WindowData> TrackStore::readWindows(const WindowKey& key, const juce::String& id)
{
    auto w = std::make_shared<WindowData>();
    bool got = false;
    if (key.fromBlocks) { //the lean record's replay block (what a shared pack contains): no stored window data needed
        LeanRecord lr; bool have = lib->readLean(id, lr);
        if (!have) have = lib->readShared(id, lr);
        if (have && unpackWindows(lr.replay, w->names, w->values)) {
            got = true; w->sr = juce::String(lr.get("window_rate")).getDoubleValue(); if (w->sr <= 0) w->sr = 96000.0;
        }
    }
    if (!got) {
        double fileRate = 0; { juce::StringArray row, head; if (indexRow(id, row, &head)) { const int c = head.indexOf("sample_rate"); if (c >= 0 && c < row.size()) fileRate = row[c].getDoubleValue(); } }
        if (!lib->readCoreAt(id, key.version, key.rate, w->names, w->values)) { if (lib->readCore(id, key.version, w->names, w->values)) { got = true; w->sr = fileRate > 0 ? fileRate : 44100.0; } }
        else { got = true; w->sr = key.rate > 0 ? (double)key.rate : (fileRate > 0 ? fileRate : 44100.0); }
    }
    if (!got || w->values.empty()) return nullptr;
    return w;
}

std::shared_ptr<const WindowData> TrackStore::windows(const WindowKey& key, const juce::String& id)
{
    const juce::String k = key.str();
    uint64_t e;
    { const juce::ScopedLock sl(windowLock); auto m = windowKept.find(k); if (m != windowKept.end()) { auto it = m->second.find(id); if (it != m->second.end()) return it->second; } e = winStamp(id); }
    auto w = readWindows(key, id);
    const juce::ScopedLock sl(windowLock);
    if (winStamp(id) != e) return w;
    auto& slot = windowKept[k][id];
    if (slot == nullptr) slot = w;
    return slot;
}

int TrackStore::windowsKept(const WindowKey& key) const
{
    const juce::ScopedLock sl(windowLock);
    auto m = windowKept.find(key.str()); return m == windowKept.end() ? 0 : (int)m->second.size();
}

int TrackStore::loadWindows(const WindowKey& key, const std::vector<juce::String>& ids, std::function<void(int, int)> progress, std::function<bool()> shouldStop)
{
    const juce::String k = key.str();
    std::vector<juce::String> todo;
    std::vector<uint64_t> stamps;
    { const juce::ScopedLock sl(windowLock); auto& m = windowKept[k]; for (auto& id : ids) if (!m.count(id)) { todo.push_back(id); stamps.push_back(winStamp(id)); } }
    if (todo.empty()) { if (progress) progress((int)ids.size(), (int)ids.size()); return 0; }
    std::atomic<int> next{0}, done{0};
    const int total = (int)todo.size();
    auto worker = [&] {
        for (;;) {
            const int i = next++;
            if (i >= total || (shouldStop && shouldStop())) return;
            auto w = readWindows(key, todo[(size_t)i]);
            { const juce::ScopedLock sl(windowLock); if (winStamp(todo[(size_t)i]) == stamps[(size_t)i]) { auto& slot = windowKept[k][todo[(size_t)i]]; if (slot == nullptr) { slot = w; } } }
            const int d = ++done; if (progress && (d % 16 == 0 || d == total)) progress(d, total);
        }
    };
    std::vector<std::thread> th;
    const unsigned n = juce::jlimit(1u, 8u, std::thread::hardware_concurrency());
    for (unsigned t = 1; t < n; ++t) th.emplace_back(worker);
    worker();
    for (auto& t : th) t.join();
    return done.load();
}

} // namespace mdark
