// Replay-block precision test: pack a track's window data at several precisions, unpack it, replay it through every Meter version's scorer and compare
// with the exact data: grade text, bonus, letter value and the tone colour (8-bit rounded, as drawn). Reports the smallest precision with NO differences
// and what each precision costs. The same check runs over a real library with: meterDARK Analyser --replay-precision <library folder>.
#include "../src/OriginalScorer.h"
#include "../src/ReplayPack.h"
#include "../src/LabHints.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <functional>
using namespace f8k;

struct Maker { const char* name; std::function<void(double, float&, float&, std::mt19937&)> f; };

int main()
{
    const double sr = 48000.0, tau = 6.283185307179586;
    const long n = (long)(20*sr);
    std::normal_distribution<float> nd(0, 1);
    std::vector<Maker> makers = {
        {"loud master", [&](double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.46), kick = std::exp(-b*14)*std::sin(tau*(48 + 90*std::exp(-b*25))*b);
            static float lp = 0; lp = 0.7f*lp + 0.3f*nd(g); l = (float)(0.55*kick + 0.16*lp + 0.05*nd(g) + 0.1*std::sin(tau*220*t)); r = (float)(0.55*kick + 0.16*lp + 0.05*nd(g) + 0.1*std::sin(tau*223*t)); }},
        {"dynamic song", [&](double t, float& l, float& r, std::mt19937& g) { const double sect = 0.35 + 0.65*(0.5 + 0.5*std::sin(tau*t/17.0)), b = std::fmod(t, 0.5), kick = std::exp(-b*18)*std::sin(tau*(55 + 80*std::exp(-b*30))*b);
            l = r = (float)(sect*(0.7*kick + 0.2*std::sin(tau*110*t)*std::exp(-std::fmod(t, 2.0)*1.5) + 0.04*nd(g))); }},
        {"sparse acoustic", [&](double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 1.7); l = r = (float)(0.5*std::exp(-b*2.2)*(std::sin(tau*196*t) + 0.5*std::sin(tau*392*t) + 0.25*std::sin(tau*588*t)) + 0.002*nd(g)); }},
        {"bright and hissy", [&](double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.25); static float hp = 0, prev = 0; float x = nd(g); hp = x - prev; prev = x;
            l = (float)(0.25*hp*std::exp(-b*10) + 0.2*std::sin(tau*3000*t)*std::exp(-b*6) + 0.05*hp); r = (float)(0.25*hp*std::exp(-b*10) + 0.2*std::sin(tau*3100*t)*std::exp(-b*6) - 0.05*hp); }},
        {"quiet rough mix", [&](double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.5), kick = std::exp(-b*16)*std::sin(tau*(50 + 90*std::exp(-b*28))*b);
            l = r = (float)(0.1*(0.8*kick + 0.3*std::sin(tau*330*t)*std::exp(-b*4) + 0.05*nd(g))); }},
    };
    const int bitsList[] = {16, 14, 12, 10};
    const int nb = 4;
    int cases[4] = {}, differ[4] = {}; double maxTone[4] = {}; size_t bytesRaw[4] = {}, bytesEst[4] = {}; double minutes = 0;
    for (auto& mv : meterVersions()) {
        for (auto& m : makers) {
            std::vector<float> L((size_t)n), R((size_t)n);
            std::mt19937 g(5);
            for (long i = 0; i < n; ++i) m.f(i/sr, L[(size_t)i], R[(size_t)i], g);
            auto base = mv.create(sr, 1);
            for (long p = 0; p < n; p += 4096) {const int c = (int)std::min<long>(4096, n - p); base->process(&L[(size_t)p], &R[(size_t)p], c);}
            const auto names = base->names;
            for (double db : {0.0, -12.0}) {
                const auto core = db == 0.0 ? base->core : scaleWindows(names, base->core, db);
                auto exact = mv.create(sr, 1);
                for (size_t w = 0; w < core.size()/names.size(); ++w) exact->feedWindow(names, &core[w*names.size()]);
                const auto a = exact->result();
                if (db == 0.0) minutes += (double)(core.size()/names.size())/23.4/60.0;
                for (int k = 0; k < nb; ++k) {
                    const auto packed = packWindows(names, core, bitsList[k]);
                    std::vector<std::string> n2; std::vector<float> c2;
                    if (!unpackWindows(packed, n2, c2) || n2 != names || c2.size() != core.size()) {printf("UNPACK FAILED\n"); return 1;}
                    auto rep = mv.create(sr, 1);
                    for (size_t w = 0; w < c2.size()/names.size(); ++w) rep->feedWindow(names, &c2[w*names.size()]);
                    const auto b = rep->result();
                    const double dt = std::max({std::fabs(a.outputR - b.outputR), std::fabs(a.outputG - b.outputG), std::fabs(a.outputB - b.outputB)});
                    const bool toneSame = std::lround(a.outputR) == std::lround(b.outputR) && std::lround(a.outputG) == std::lround(b.outputG) && std::lround(a.outputB) == std::lround(b.outputB);
                    const bool same = a.grade == b.grade && a.bonusValue == b.bonusValue && toneSame;
                    ++cases[k]; if (!same) ++differ[k]; maxTone[k] = std::max(maxTone[k], dt);
                    if (db == 0.0) {bytesRaw[k] += packed.size(); bytesEst[k] += estimateCompressedBytes(packed);}
                    if (!same && bitsList[k] >= 12) printf("  differs at %d bits: %s %s %+.0f dB  %s vs %s  tone %.2f\n", bitsList[k], mv.version.c_str(), m.name, db, a.grade.c_str(), b.grade.c_str(), dt);
                }
            }
        }
    }
    printf("\nReplay block precision (%0.f minutes of synthetic music, %zu Meter versions):\n  bits  cases  differing  max tone diff  packed KB/min  compressed (est.) KB/min\n", minutes, meterVersions().size());
    int best = -1;
    for (int k = 0; k < nb; ++k) {
        printf("  %4d  %5d  %9d  %13.3f  %13.1f  %14.1f\n", bitsList[k], cases[k], differ[k], maxTone[k], bytesRaw[k]/1024.0/minutes*(double)meterVersions().size()/1.0/(double)meterVersions().size(), bytesEst[k]/1024.0/minutes);
        if (differ[k] == 0) best = k;
    }
    // sizes above are per minute of one version's data
    if (best < 0) {printf("NO precision reproduced every grade.\n"); return 1;}
    printf("Smallest precision with no differences here: %d bits. A 4-minute track: about %.0f KB compressed.\nPACK TEST PASSED (16 bits reproduces everything)\n", bitsList[best], bytesEst[best]/1024.0/minutes*4.0);
    return differ[0] == 0 ? 0 : 1;
}
