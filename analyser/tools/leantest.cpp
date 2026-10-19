// Lean record test: build a record from a synthetic track's windows, write and read it back, check the replay block regrades to the same grade, and print the size.
#include "../src/LeanRecord.h"
#include <cstdio>
#include <cmath>
#include <random>
using namespace f8k;
int main()
{
    const double sr = 96000.0, tau = 6.283185307179586; const long n = (long)(120*sr);
    std::mt19937 g(3); std::normal_distribution<float> nd(0, 1);
    int bad = 0;
    for (auto& mv : meterVersions()) {
        auto run = mv.create(sr, 1);
        std::vector<float> L(4096), R(4096);
        for (long p = 0; p < n; p += 4096) {
            for (int i = 0; i < 4096; ++i) { const double t = (double)(p + i)/sr, b = std::fmod(t, 0.5), sect = 0.4 + 0.6*(0.5 + 0.5*std::sin(tau*t/23.0));
                L[(size_t)i] = R[(size_t)i] = (float)(sect*(0.6*std::exp(-b*16)*std::sin(tau*(50 + 90*std::exp(-b*28))*b) + 0.04*nd(g))); }
            run->process(L.data(), R.data(), 4096);
        }
        const std::string exactGrade = run->result().grade;
        auto rec = buildLeanRecord(mv, sr, run->names, run->core, {{"id", "test"}, {"title", "synthetic"}});
        auto bytes = serialiseLean(rec);
        LeanRecord back;
        if (!parseLean(bytes, back) || back.columns != rec.columns || back.toneRgb != rec.toneRgb || back.replay != rec.replay || back.get("title") != "synthetic") {printf("ROUNDTRIP FAILED %s\n", mv.version.c_str()); ++bad; continue;}
        std::vector<std::string> n2; std::vector<float> c2;
        if (!unpackWindows(back.replay, n2, c2)) {printf("UNPACK FAILED\n"); ++bad; continue;}
        auto rep = mv.create(sr, 1);
        for (size_t w = 0; w < c2.size()/n2.size(); ++w) rep->feedWindow(n2, &c2[w*n2.size()]);
        const bool same = rep->result().grade == exactGrade && back.get("grade") == exactGrade;
        size_t displayBytes = bytes.size() - back.replay.size();
        printf("Meter %s: %d columns, grade %s, display part %zu bytes (%zu est. compressed), replay block %zu bytes (%zu est. compressed) %s\n", mv.version.c_str(), rec.columns, exactGrade.c_str(),
               displayBytes, estimateCompressedBytes(std::vector<uint8_t>(bytes.begin(), bytes.end() - (long)back.replay.size())), back.replay.size(), estimateCompressedBytes(back.replay), same ? "OK" : "GRADE DIFFERS");
        if (!same) ++bad;
    }
    printf(bad ? "LEAN TEST FAILED\n" : "LEAN TEST PASSED\n");
    return bad ? 1 : 0;
}
