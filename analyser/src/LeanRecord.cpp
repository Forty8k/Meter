#include "LeanRecord.h"
#include "LabHints.h"
#include <cmath>
#include <algorithm>

namespace f8k {

static int nameIndex(const std::vector<std::string>& names, const char* n) { for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int)i; return -1; }

LeanRecord buildLeanRecord(const MeterVersion& version, double sampleRate, const std::vector<std::string>& names, const std::vector<float>& windows,
                           const std::vector<std::pair<std::string, std::string>>& meta, int maxColumns, int packBits, bool includeReplay)
{
    LeanRecord rec; rec.meta = meta;
    const size_t nc = names.size(), nw = nc ? windows.size()/nc : 0;
    if (nw == 0) return rec;
    const int iPL = nameIndex(names, "PEAK_LEFT"), iPR = nameIndex(names, "PEAK_RIGHT"), iSL = nameIndex(names, "SLEW_LEFT"), iSR = nameIndex(names, "SLEW_RIGHT");
    const int iZL = nameIndex(names, "ZERO_LEFT"), iZR = nameIndex(names, "ZERO_RIGHT");
    if (iPL < 0 || iPR < 0 || iSL < 0 || iSR < 0 || iZL < 0 || iZR < 0) return rec;
    const HintSettings hs; //the plugin's own constants for the quiet fade
    const float ceilRise = (float)hs["ceilRise"], ceilFall = (float)hs["ceilFall"];

    rec.columns = (int)std::min<size_t>(nw, (size_t)std::max(1, maxColumns));
    const size_t N = (size_t)rec.columns;
    struct Acc { double r = 0, g = 0, b = 0, dw = 0, dp = 0, ds = 0, lp = 0, lb = 0, ls = 0; int n = 0; };
    std::vector<Acc> acc(N);

    auto run = version.create(sampleRate, 1);
    float massCeil = 0.0f;
    for (size_t w = 0; w < nw; ++w) {
        const float* v = &windows[w*nc];
        run->feedWindow(names, v);
        const OriginalResult o = run->result();
        // the same per-window numbers the plugin's columnMetrics / scoreColumn work out
        const float peakL = v[iPL]*200.0f, peakR = v[iPR]*200.0f, slewL = std::sqrt(v[iSL])*300.0f, slewR = std::sqrt(v[iSR])*300.0f;
        auto bass = [](float z) {float mz = std::sqrt(std::max(z, 0.0f))*6.0f - 6.0f; if (mz > 192.0f) mz = 192.0f; return std::min(std::sqrt(std::max(mz, 0.0f))*16.2f - 20.0f, 199.0f);};
        const float bassL = bass(v[iZL]), bassR = bass(v[iZR]);
        const float topPeak = std::max(peakL, peakR);
        if (topPeak > 8.0f) { if (topPeak > massCeil) massCeil += (topPeak - massCeil)*ceilRise; else massCeil -= (massCeil - topPeak)*ceilFall; }
        float quiet;
        { const float p = std::max(std::max(v[iPL], v[iPR]), std::max(v[iSL], v[iSR]));
          if (massCeil > 0.0f) { const float ceilP = std::max(std::min(massCeil/200.0f, 0.94f), 0.05f); quiet = std::min(p*8.0f*0.94f/ceilP, 1.0f); }
          else quiet = std::min(p*8.0f, 1.0f); }
        const double scoreMax = std::max(std::max(o.outputR, o.outputG), std::max(o.outputB, 0.000001));
        const float rZ = (float)(o.outputG/scoreMax), rP = (float)(o.outputB/scoreMax), rS = (float)(o.outputR/scoreMax);
        const size_t col = std::min(N - 1, w*N/nw);
        Acc& a = acc[col];
        a.r += std::min(std::max(o.storeR, 0.0), 1.0); a.g += std::min(std::max(o.storeG, 0.0), 1.0); a.b += std::min(std::max(o.storeB, 0.0), 1.0);
        a.dw += (rZ - (rP + rS)*0.5f)*quiet; a.dp += (rP - (rZ + rS)*0.5f)*quiet; a.ds += (rS - (rZ + rP)*0.5f)*quiet;
        a.lp += topPeak; a.lb += 0.5f*(bassL + bassR); a.ls += std::min(0.5f*(slewL + slewR), 200.0f);
        ++a.n;
    }
    rec.toneRgb.resize(N*3); rec.devWeight.resize(N); rec.devPeaks.resize(N); rec.devSlew.resize(N); rec.linePeak.resize(N); rec.lineBass.resize(N); rec.lineSlew.resize(N);
    auto u8 = [](double x) {return (uint8_t)std::min(255.0, std::max(0.0, std::round(x)));};
    auto s8 = [](double x) {return (int8_t)std::min(127.0, std::max(-127.0, std::round(x*127.0)));};
    for (size_t c = 0; c < N; ++c) {
        const Acc& a = acc[c]; const double k = a.n ? 1.0/a.n : 0.0;
        rec.toneRgb[c*3] = u8(a.r*k*255.0); rec.toneRgb[c*3 + 1] = u8(a.g*k*255.0); rec.toneRgb[c*3 + 2] = u8(a.b*k*255.0);
        rec.devWeight[c] = s8(a.dw*k); rec.devPeaks[c] = s8(a.dp*k); rec.devSlew[c] = s8(a.ds*k);
        rec.linePeak[c] = u8(a.lp*k); rec.lineBass[c] = u8(a.lb*k); rec.lineSlew[c] = u8(a.ls*k);
    }
    const OriginalResult fin = run->result();
    rec.set("meter_version", version.version);
    rec.set("grade", fin.grade);
    rec.set("bonus_value", std::to_string(fin.bonusValue));
    rec.set("letter_value", std::to_string(fin.letterValue));
    rec.set("authority_value", std::to_string(fin.bassPrevGrade));
    rec.set("power_value", std::to_string(fin.peakPrevGrade));
    rec.set("detail_value", std::to_string(fin.slewPrevGrade));
    rec.set("windows", std::to_string(nw));
    if (includeReplay) rec.replay = packWindows(names, windows, packBits);
    return rec;
}

static void putStr(std::vector<uint8_t>& o, const std::string& s) { rpPutVar(o, s.size()); o.insert(o.end(), s.begin(), s.end()); }
static bool getStr(const uint8_t*& p, const uint8_t* e, std::string& s) { uint64_t n; if (!rpGetVar(p, e, n) || (uint64_t)(e - p) < n) return false; s.assign((const char*)p, (size_t)n); p += n; return true; }

std::vector<uint8_t> serialiseLean(const LeanRecord& r)
{
    std::vector<uint8_t> o;
    o.push_back('L'); o.push_back('N'); o.push_back('R'); o.push_back(1);
    rpPutVar(o, r.meta.size());
    for (auto& kv : r.meta) {putStr(o, kv.first); putStr(o, kv.second);}
    rpPutVar(o, (uint64_t)r.columns);
    o.insert(o.end(), r.toneRgb.begin(), r.toneRgb.end());
    for (auto* v : {&r.devWeight, &r.devPeaks, &r.devSlew}) for (int8_t x : *v) o.push_back((uint8_t)x);
    for (auto* v : {&r.linePeak, &r.lineBass, &r.lineSlew}) o.insert(o.end(), v->begin(), v->end());
    rpPutVar(o, r.replay.size()); o.insert(o.end(), r.replay.begin(), r.replay.end());
    return o;
}

bool parseLean(const std::vector<uint8_t>& in, LeanRecord& r)
{
    r = LeanRecord();
    if (in.size() < 6 || in[0] != 'L' || in[1] != 'N' || in[2] != 'R' || in[3] != 1) return false;
    const uint8_t* p = in.data() + 4; const uint8_t* e = in.data() + in.size();
    uint64_t n;
    if (!rpGetVar(p, e, n)) return false;
    for (uint64_t i = 0; i < n; ++i) { std::string k, v; if (!getStr(p, e, k) || !getStr(p, e, v)) return false; r.meta.emplace_back(k, v); }
    uint64_t cols; if (!rpGetVar(p, e, cols) || cols > 100000 || (uint64_t)(e - p) < cols*12) return false;
    r.columns = (int)cols; const size_t N = (size_t)cols;
    r.toneRgb.assign(p, p + N*3); p += N*3;
    for (auto* v : {&r.devWeight, &r.devPeaks, &r.devSlew}) { v->resize(N); for (size_t i = 0; i < N; ++i) (*v)[i] = (int8_t)*p++; }
    for (auto* v : {&r.linePeak, &r.lineBass, &r.lineSlew}) { v->assign(p, p + N); p += N; }
    uint64_t rl; if (!rpGetVar(p, e, rl) || (uint64_t)(e - p) < rl) return false;
    r.replay.assign(p, p + rl);
    return true;
}

} // namespace f8k
