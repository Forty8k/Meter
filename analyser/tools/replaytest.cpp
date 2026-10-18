// Replay-with-gain test: the Library/Lab level modes score a track by scaling its STORED windows. Is that the same as really scaling the audio first?
// For synthetic music-like tracks and every Meter version: gains 0, -6 ... -24 dB (and +6, +12), plus the "mix" mode. Prints the grade both ways and the
// differences in letter, bonus, power, detail and authority. Chris's slew line has an absolute threshold, so BSLW/DSLW cannot scale exactly.
#include "../src/OriginalScorer.h"
#include "../src/LabHints.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <functional>
#include <algorithm>
using namespace f8k;

struct Maker { const char* name; std::function<void(double, float&, float&, std::mt19937&)> f; };

int main()
{
    const double sr = 44100.0, tau = 6.283185307179586;
    const long n = (long)(30*sr);
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
    const double dbs[] = {0, -6, -12, -18, -24, 6, 12};
    double worstLetter = 0, worstPower = 0, worstDetail = 0, worstAuth = 0, worstBonus = 0; int cases = 0, gradeSame = 0;
    for (auto& mv : meterVersions()) {
        printf("\nMeter %s   (grade: really scaled audio | replayed stored windows)\n", mv.version.c_str());
        printf("  %-18s %-6s %-26s %-26s  dLetter dPower dDetail dAuthority dBonus\n", "track", "level", "audio scaled", "windows replayed");
        for (auto& m : makers) {
            std::vector<float> L((size_t)n), R((size_t)n);
            std::mt19937 g(5);
            for (long i = 0; i < n; ++i) m.f(i/sr, L[(size_t)i], R[(size_t)i], g);
            auto base = mv.create(sr, 1);
            for (long p = 0; p < n; p += 4096) {const int c = (int)std::min<long>(4096, n - p); base->process(&L[(size_t)p], &R[(size_t)p], c);}
            const auto names = base->names; const auto core = base->core;
            auto doOne = [&](const char* label, const std::function<void(std::vector<float>&, std::vector<float>&)>& scaleAudio, const std::vector<float>& replayCore) {
                std::vector<float> sl = L, sr2 = R; scaleAudio(sl, sr2);
                auto real = mv.create(sr, 1);
                for (long p = 0; p < n; p += 4096) {const int c = (int)std::min<long>(4096, n - p); real->process(&sl[(size_t)p], &sr2[(size_t)p], c);}
                auto rep = mv.create(sr, 1);
                for (size_t w = 0; w < replayCore.size()/names.size(); ++w) rep->feedWindow(names, &replayCore[w*names.size()]);
                const auto a = real->result(), b = rep->result();
                const double dl = std::fabs(a.letterValue - b.letterValue), dp = std::fabs(a.peakPrevGrade - b.peakPrevGrade), dd = std::fabs(a.slewPrevGrade - b.slewPrevGrade), da = std::fabs(a.bassPrevGrade - b.bassPrevGrade), db2 = std::fabs((double)a.bonusValue - b.bonusValue);
                printf("  %-18s %-6s %-26s %-26s  %6.2f %6.2f %7.2f %10.2f %6.0f\n", m.name, label, a.grade.c_str(), b.grade.c_str(), dl, dp, dd, da, db2);
                worstLetter = std::max(worstLetter, dl); worstPower = std::max(worstPower, dp); worstDetail = std::max(worstDetail, dd); worstAuth = std::max(worstAuth, da); worstBonus = std::max(worstBonus, db2);
                ++cases; if (a.grade == b.grade) ++gradeSame;
            };
            for (double db : dbs) {
                const double gain = std::pow(10.0, db/20.0);
                char lab[16]; snprintf(lab, sizeof lab, "%+.0f dB", db);
                doOne(lab, [&](std::vector<float>& l, std::vector<float>& r) { for (auto& x : l) x = (float)(x*gain); for (auto& x : r) x = (float)(x*gain); }, scaleWindows(names, core, db));
            }
            { //mix: the plugin's floating gain on the audio (64-sample stretches) against the gain worked out from the stored windows
                std::vector<double> none;
                doOne("mix", [&](std::vector<float>& l, std::vector<float>& r) {
                    double ceiling = 0, hold = 0;
                    for (size_t s = 0; s < l.size(); s += 64) {
                        const size_t c = std::min<size_t>(64, l.size() - s); float pk = 0;
                        for (size_t i = 0; i < c; ++i) pk = std::max(pk, std::max(std::fabs(l[s + i]), std::fabs(r[s + i])));
                        if (pk > ceiling) {ceiling = pk; hold = 8.0*sr;} else if (hold > 0) hold -= (double)c; else ceiling *= std::exp(-(double)c/(20.0*sr));
                        const double gm = std::min(15.848931924611133, std::max(0.1, 0.8912509381337456/std::max(ceiling, 1.0e-9)));
                        for (size_t i = 0; i < c; ++i) {l[s + i] = (float)(l[s + i]*gm); r[s + i] = (float)(r[s + i]*gm);}
                    }
                }, scaleWindowsEach(names, core, mixWindowGains(names, core, sr)));
            }
        }
    }
    printf("\nreplay vs really scaled audio: %d cases, identical grade text in %d. Largest differences: letter %.2f, power %.2f, detail %.2f, authority %.2f, bonus %.0f\n",
           cases, gradeSame, worstLetter, worstPower, worstDetail, worstAuth, worstBonus);
    return (worstLetter < 0.3 && worstPower < 0.3 && worstDetail < 0.3 && worstAuth < 0.3) ? 0 : 1;
}
