// Level test on synthetic tracks (no library needed): how far do the Lab's hint readings move when the same music is played quieter?
// Prints the numbers for the old hint maths (fixed gates) and the level-independent maths.
#include "../src/OriginalScorer.h"
#include "../src/LabHints.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <functional>
using namespace f8k;

struct Maker { const char* name; std::function<void(long, double, float&, float&, std::mt19937&)> f; };

int main()
{
    const double sr = 44100.0, tau = 6.283185307179586;
    const long n = (long)(75*sr);
    std::mt19937 rng(11); std::normal_distribution<float> nd(0, 1);
    std::vector<Maker> makers = {
        {"loud master (dense, bright)", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.46), kick = std::exp(-b*14)*std::sin(tau*(48 + 90*std::exp(-b*25))*b);
            static float lp = 0; lp = 0.7f*lp + 0.3f*nd(g); l = (float)(0.55*kick + 0.16*lp + 0.05*nd(g) + 0.1*std::sin(tau*220*t)); r = (float)(0.55*kick + 0.16*lp + 0.05*nd(g) + 0.1*std::sin(tau*223*t)); }},
        {"dynamic song (quiet and loud parts)", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double sect = 0.35 + 0.65*(0.5 + 0.5*std::sin(tau*t/17.0)), b = std::fmod(t, 0.5), kick = std::exp(-b*18)*std::sin(tau*(55 + 80*std::exp(-b*30))*b);
            l = r = (float)(sect*(0.7*kick + 0.2*std::sin(tau*110*t)*std::exp(-std::fmod(t, 2.0)*1.5) + 0.04*nd(g))); }},
        {"sparse acoustic (decaying notes)", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 1.7); l = r = (float)(0.5*std::exp(-b*2.2)*(std::sin(tau*196*t) + 0.5*std::sin(tau*392*t) + 0.25*std::sin(tau*588*t)) + 0.002*nd(g)); }},
        {"bass heavy", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.5); l = r = (float)(0.8*std::sin(tau*(42 + 6*std::sin(tau*0.25*t))*t)*(0.6 + 0.4*std::exp(-b*6)) + 0.03*nd(g)); }},
        {"bright and hissy", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.25); static float hp = 0, prev = 0; float x = nd(g); hp = x - prev; prev = x;
            l = (float)(0.25*hp*std::exp(-b*10) + 0.2*std::sin(tau*3000*t)*std::exp(-b*6) + 0.05*hp); r = (float)(0.25*hp*std::exp(-b*10) + 0.2*std::sin(tau*3100*t)*std::exp(-b*6) - 0.05*hp); }},
        {"drum stem (hits and silence)", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.5), h = std::fmod(t + 0.25, 0.5); const double kick = std::exp(-b*20)*std::sin(tau*(52 + 100*std::exp(-b*35))*b);
            l = r = (float)(0.9*kick + 0.3*std::exp(-h*60)*nd(g)); }},
        {"quiet rough mix (-20 dB master)", [&](long i, double t, float& l, float& r, std::mt19937& g) { const double b = std::fmod(t, 0.5), kick = std::exp(-b*16)*std::sin(tau*(50 + 90*std::exp(-b*28))*b);
            l = r = (float)(0.1*(0.8*kick + 0.3*std::sin(tau*330*t)*std::exp(-b*4) + 0.05*nd(g))); }},
        {"squashed loud (flat peaks)", [&](long i, double t, float& l, float& r, std::mt19937& g) { double x = 0.9*std::sin(tau*70*t) + 0.5*std::sin(tau*1400*t) + 0.3*nd(g); x = std::tanh(2.5*x)*0.95; l = (float)x; r = (float)(std::tanh(2.5*(0.9*std::sin(tau*70*t + 0.3) + 0.5*std::sin(tau*1500*t) + 0.3*nd(g)))*0.95); }},
    };
    std::vector<std::vector<std::string>> names; std::vector<std::vector<float>> cores;
    const auto& mv = meterVersions().front();
    for (auto& m : makers) {
        std::vector<float> L((size_t)n), R((size_t)n);
        std::mt19937 g(5);
        for (long i = 0; i < n; ++i) m.f(i, i/sr, L[(size_t)i], R[(size_t)i], g);
        auto run = mv.create(sr, 1);
        for (long p = 0; p < n; p += 4096) {const int c = (int)std::min<long>(4096, n - p); run->process(&L[(size_t)p], &R[(size_t)p], c);}
        names.push_back(run->names); cores.push_back(run->core);
    }
    std::vector<LevelTestTrack> tr; for (size_t i = 0; i < names.size(); ++i) tr.push_back({&names[i], &cores[i]});
    const std::vector<double> dbs = {0, -6, -12, -18, -24};
    HintSettings before, after;
    before.v[(size_t)hintParamIndex("fMax")] = 4; before.v[(size_t)hintParamIndex("relativeGates")] = 0; before.v[(size_t)hintParamIndex("detailLevelComp")] = 0;
    printf("BEFORE (fixed gates, fMax 4)\n%s\n", levelTestReport(levelTest(tr, before, dbs)).c_str());
    printf("AFTER (gates follow the level, detail level match, fMax 16)\n%s\n", levelTestReport(levelTest(tr, after, dbs)).c_str());
    // each part on its own
    struct Step {const char* label; double fMax, gates, comp;} steps[] = {{"only fMax 16", 16, 0, 0}, {"fMax 16 + gates", 16, 1, 0}, {"fMax 16 + detail match", 16, 0, 1}};
    for (auto& st : steps) {HintSettings s; s.v[(size_t)hintParamIndex("fMax")] = st.fMax; s.v[(size_t)hintParamIndex("relativeGates")] = st.gates; s.v[(size_t)hintParamIndex("detailLevelComp")] = st.comp;
        printf("%s\n%s\n", st.label, levelTestReport(levelTest(tr, s, dbs)).c_str());}
    // per track, at -18 dB
    printf("Per track at -18 dB: readings (weight, dynamics, dark <-> bright) at 0 dB / -18 dB, before then after\n");
    for (size_t i = 0; i < names.size(); ++i) {
        for (int pass = 0; pass < 2; ++pass) {
            const HintSettings& s = pass == 0 ? before : after;
            const auto a = simulateHints(names[i], cores[i], s), b = simulateHints(names[i], scaleWindows(names[i], cores[i], -18), s);
            printf("  %-38s %s  W %.2f/%.2f  D %.2f/%.2f  tilt %.2f/%.2f   audible %d/%d\n", makers[i].name, pass == 0 ? "before" : "after ", a.weight, b.weight, a.dynamics, b.dynamics, a.tilt, b.tilt, a.audible, b.audible);
        }
    }
    // pass/fail for CI: with the level-independent maths the Detail readings must hardly move at any level, and weight/dynamics must not move much
    const auto rows = levelTest(tr, after, dbs);
    bool ok = true;
    for (auto& r : rows) {
        if (r.readingAbs[2] > 0.05 || r.readingAbs[0] > 0.1 || r.readingAbs[1] > 0.05) ok = false;
        for (int i = 0; i < 6; ++i) if (r.onAbs[i] > 0.15) ok = false;
    }
    printf("%s\n", ok ? "LEVEL TEST PASSED" : "LEVEL TEST FAILED");
    return ok ? 0 : 1;
}
