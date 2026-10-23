// Self-test: (1) rebuilding the core windows from fine blocks matches the plugin's own windows,
// (2) the original scorer runs and gives sensible results on test tones. Build: see tools/run_selftest.sh
#include "../src/Resampler.h"
#include "../src/Capture.h"
#include "../src/OriginalScorer.h"
#include "../src/GradeRank.h"
#include <cstdio>
#include <random>
#include <functional>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace mdark;

static CoreWindow mergeFine(const Capture& c, size_t firstBlock, size_t numBlocks, double sr)
{
    const int S = F_CH_STRIDE; const double a = 1.0/sr, zs = 44100.0/sr;
    CoreWindow w{};
    float* out[2][5] = {{&w.peakL, &w.slewL, &w.zeroL, &w.beamL, &w.deptL}, {&w.peakR, &w.slewR, &w.zeroR, &w.beamR, &w.deptR}};
    for (int ch = 0; ch < 2; ++ch) {
        double peak = 0, slew = 0, beam = 0, dept = 0, longest = 0; long N = 0, lastAbs = -1, firstAbs = -1;
        for (size_t b = firstBlock; b < firstBlock + numBlocks; ++b) N += (long)c.fine[b*F_NUM_COLS + F_N];
        long off = 0;
        for (size_t b = firstBlock; b < firstBlock + numBlocks; ++b) {
            const float* r = &c.fine[b*F_NUM_COLS]; const float* q = r + F_PEAK + ch*S; const long n = (long)r[F_N];
            peak = std::max(peak, (double)q[0]); slew = std::max(slew, (double)q[5]); beam = q[11];
            dept += q[10]*std::pow(1.0 - a, (double)(N - off - n));
            if (q[6] > 0) {
                const long f = off + (long)q[7], l = off + (long)q[8];
                if (firstAbs < 0) firstAbs = f; else longest = std::max(longest, (double)(f - lastAbs));
                longest = std::max(longest, (double)q[9]);
                lastAbs = l;
            }
            off += n;
        }
        if (firstAbs < 0) longest = (double)N; else {longest = std::max(longest, (double)(firstAbs + 1)); longest = std::max(longest, (double)(N - 1 - lastAbs));}
        *out[ch][0] = (float)std::sqrt(peak); *out[ch][1] = (float)slew; *out[ch][2] = (float)(longest*zs); *out[ch][3] = (float)beam; *out[ch][4] = (float)dept;
    }
    return w;
}

static double gWorst = 0;
static void runSignal(const char* name, double sr, double seconds, std::function<void(long, float&, float&)> gen, bool checkMerge)
{
    const long totalS = (long)(seconds*sr);
    std::vector<float> sigL((size_t)totalS), sigR((size_t)totalS);
    for (long i = 0; i < totalS; ++i) gen(i, sigL[(size_t)i], sigR[(size_t)i]);
    gen = [&](long i, float& l, float& r) { l = sigL[(size_t)i]; r = sigR[(size_t)i]; }; //every pass below sees the same audio
    Capture cap(sr, checkMerge ? 941 : 0);
    std::vector<float> L(4096), R(4096);
    const long total = (long)(seconds*sr);
    for (long pos = 0; pos < total; pos += 4096) {
        const int n = (int)std::min<long>(4096, total - pos);
        for (int i = 0; i < n; ++i) gen(pos + i, L[i], R[i]);
        cap.process(L.data(), R.data(), n);
    }
    cap.finish();
    double worst = 0;
    if (checkMerge && sr == 44100.0) {
        for (size_t w = 0; w < cap.core.size(); ++w) {
            const CoreWindow m = mergeFine(cap, w*2, 2, sr), &o = cap.core[w];
            const float* A = &o.peakL; const float* B = &m.peakL;
            for (int k = 0; k < 10; ++k) worst = std::max(worst, (double)std::fabs(A[k] - B[k]) / std::max(1e-9, (double)std::fabs(A[k])));
        }
    }
    gWorst = std::max(gWorst, worst);
    //the original 0.5.5's own audio loop must give exactly the windows our copy gives, and replaying them must give the same grade
    auto run = meterVersions().front().create(sr, 1);
    {
        std::vector<float> L(4096), R(4096);
        for (long pos = 0; pos < total; pos += 4096) {
            const int n = (int)std::min<long>(4096, total - pos);
            for (int i = 0; i < n; ++i) gen(pos + i, L[i], R[i]);
            run->process(L.data(), R.data(), n);
        }
    }
    double coreDiff = 0;
    if (run->windowCount() != cap.core.size()) coreDiff = 1;
    else for (size_t w = 0; w < cap.core.size(); ++w) { const float* A = &cap.core[w].peakL; for (int k = 0; k < 10; ++k) coreDiff = std::max(coreDiff, (double)std::fabs(A[k] - run->core[w*10 + k])); }
    gWorst = std::max(gWorst, coreDiff);
    const OriginalResult r = run->result();
    auto replay = meterVersions().front().create(sr, 1);
    for (size_t w = 0; w < run->windowCount(); ++w) replay->feedWindow(run->names, &run->core[w*run->names.size()]);
    if (replay->result().grade != r.grade) {printf("REPLAY MISMATCH %s vs %s\n", replay->result().grade.c_str(), r.grade.c_str()); gWorst = 1;}
    std::string others;
    for (size_t v = 1; v < meterVersions().size(); ++v) {
        auto o = meterVersions()[v].create(sr, 1);
        std::vector<float> L(4096), R(4096);
        for (long pos = 0; pos < total; pos += 4096) { const int n = (int)std::min<long>(4096, total - pos); for (int i = 0; i < n; ++i) gen(pos + i, L[i], R[i]); o->process(L.data(), R.data(), n); }
        others += "  " + meterVersions()[v].version + "=\"" + o->result().grade + "\" " + o->result().directionsMain;
    }
    printf("%-22s windows=%zu  0.5.5=\"%s\"  power=%.2f detail=%.2f authority=%.2f  peak=%.1f dBFS  LUFS=%.1f%s\n", name, cap.core.size(), r.grade.c_str(),
           r.peakPrevGrade, r.slewPrevGrade, r.bassPrevGrade, 20*std::log10(std::max(cap.peakAbs, 1e-9)), cap.integratedLufs(),
           (checkMerge ? (std::string("  merge-err=") + std::to_string(worst)).c_str() : ""));
    printf("    %s%s\n", r.directionsMain.c_str(), others.c_str());
}


// Rate invariance: the same band-limited music-like signal made at 44.1, 48 and 96 kHz. Read at its own rate the Meter versions disagree;
// converted to one anchor rate first (the analyser's rate runs) they must agree closely. Prints every number.
static bool rateInvariance()
{
    const double rates[3] = {44100.0, 48000.0, 96000.0};
    const int anchors[2] = {44100, 96000};
    const double seconds = 40.0;
    const int nPart = 70;
    std::mt19937 rng(7); std::uniform_real_distribution<double> ur(0.0, 1.0);
    struct P { double f, a, ph, pan, rate, depth; } parts[nPart];
    for (int i = 0; i < nPart; ++i) parts[i] = {60.0*std::pow(17000.0/60.0, (double)i/(nPart - 1)) * (0.97 + 0.06*ur(rng)), std::pow(10.0, -(0.5 + 1.5*ur(rng))), ur(rng)*6.2831853, ur(rng), 0.5 + 3.0*ur(rng), ur(rng)};
    auto makeSignal = [&](double sr, std::vector<float>& L, std::vector<float>& R) {
        const long n = (long)(seconds*sr); L.assign((size_t)n, 0.f); R.assign((size_t)n, 0.f);
        for (long i = 0; i < n; ++i) {
            const double t = i/sr; double l = 0, r = 0;
            const double beat = std::fmod(t, 0.5), hit = std::exp(-beat*18.0)*(0.5 - 0.5*std::cos(3.14159265358979*std::min(beat/0.008, 1.0))); //8 ms smooth attack: no content above ~20 kHz
            for (auto& p : parts) {
                const double env = (0.35 + 0.65*hit)*(1.0 - p.depth*0.5 + 0.5*p.depth*std::sin(6.2831853*p.rate*0.2*t + p.ph));
                const double v = p.a*env*std::sin(6.2831853*p.f*t + p.ph);
                l += v*(1.0 - 0.5*p.pan); r += v*(0.5 + 0.5*p.pan);
            }
            L[(size_t)i] = (float)(0.8*l); R[(size_t)i] = (float)(0.8*r);
        }
    };
    struct Row { std::string grade; double letter, power, detail, authority; int bonus; };
    auto runAt = [&](const MeterVersion& mv, double sr, const std::vector<float>& L, const std::vector<float>& R) {
        auto run = mv.create(sr, 1);
        for (size_t pos = 0; pos < L.size(); pos += 65536) { const int n = (int)std::min<size_t>(65536, L.size() - pos); run->process(&L[pos], &R[pos], n); }
        const auto r = run->result();
        return Row{r.grade, r.letterValue, r.peakPrevGrade, r.slewPrevGrade, r.bassPrevGrade, r.bonusValue};
    };
    bool ok = true;
    double worstAnchor = 0, worstNative = 0;
    for (auto& mv : meterVersions()) {
        std::vector<std::vector<float>> L(3), R(3);
        for (int k = 0; k < 3; ++k) makeSignal(rates[k], L[k], R[k]);
        printf("\nRate invariance, Meter %s (same signal made at three rates)\n", mv.version.c_str());
        printf("  %-18s %-24s letter  power  detail  authority  bonus\n", "read at", "grade");
        std::vector<Row> nat; 
        for (int k = 0; k < 3; ++k) {
            nat.push_back(runAt(mv, rates[k], L[k], R[k]));
            printf("  %-18s %-24s %6.2f %6.2f %6.2f %9.2f %6d\n", (std::to_string((int)rates[k]) + " (own)").c_str(), nat.back().grade.c_str(), nat.back().letter, nat.back().power, nat.back().detail, nat.back().authority, nat.back().bonus);
        }
        for (int a : anchors) {
            std::vector<Row> rows;
            for (int k = 0; k < 3; ++k) {
                if (std::lround(rates[k]) == a) {rows.push_back(nat[(size_t)k]); continue;}
                mdarkdsp::Resampler rl(rates[k], a), rr(rates[k], a);
                std::vector<float> cl, cr;
                for (size_t pos = 0; pos < L[k].size(); pos += 65536) { const size_t n = std::min<size_t>(65536, L[k].size() - pos); rl.process(&L[k][pos], n, cl); rr.process(&R[k][pos], n, cr); }
                rl.flush(cl); rr.flush(cr);
                rows.push_back(runAt(mv, a, cl, cr));
            }
            for (int k = 0; k < 3; ++k) printf("  %-18s %-24s %6.2f %6.2f %6.2f %9.2f %6d\n", ("-> " + std::to_string(a) + " from " + std::to_string((int)rates[k])).c_str(), rows[(size_t)k].grade.c_str(), rows[(size_t)k].letter, rows[(size_t)k].power, rows[(size_t)k].detail, rows[(size_t)k].authority, rows[(size_t)k].bonus);
            double spread = 0;
            for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
                spread = std::max({spread, std::fabs(rows[(size_t)i].power - rows[(size_t)j].power), std::fabs(rows[(size_t)i].detail - rows[(size_t)j].detail), std::fabs(rows[(size_t)i].authority - rows[(size_t)j].authority)});
            double natSpread = 0;
            for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
                natSpread = std::max({natSpread, std::fabs(nat[(size_t)i].power - nat[(size_t)j].power), std::fabs(nat[(size_t)i].detail - nat[(size_t)j].detail), std::fabs(nat[(size_t)i].authority - nat[(size_t)j].authority)});
            printf("  anchor %d: largest difference of power/detail/authority across start rates = %.4f   (read at own rates: %.4f)\n", a, spread, natSpread);
            worstAnchor = std::max(worstAnchor, spread); worstNative = std::max(worstNative, natSpread);
        }
    }
    printf("\nrate invariance: converted-to-anchor spread %.4f, own-rate spread %.4f\n", worstAnchor, worstNative);
    return ok && worstAnchor < 0.05 && worstAnchor <= worstNative;
}

// The ranked grade parts used by the Library filters (GradeRank.h): one rank per letter that does not depend on the Meter version.
static bool gradeRankTest()
{
    bool ok = true;
    auto check = [&](bool c, const char* what) {if (!c) {printf("GRADE RANK FAILED: %s\n", what); ok = false;}};
    int a = 0, b = 0;
    // sub-grades: ... c < b < a < A < B ... (capitals above lowercase), higher is better
    check(subGradeRank("a", a) && subGradeRank("A", b) && b > a, "A above a");
    check(subGradeRank("b", a) && subGradeRank("a", b) && b > a, "a above b");
    check(subGradeRank("s", a) && subGradeRank("b", b) && b > a, "b above s");
    check(subGradeRank("M", a) && subGradeRank("K", b) && a > b, "M above K");
    check(subGradeRank(" B ", a) && a == 2, "B is rank 2 (spaces ignored)");
    check(!subGradeRank("", a) && !subGradeRank("bb", a) && !subGradeRank("7", a), "not a sub-grade letter");
    // every consecutive pair on the offered scale is strictly descending
    const auto scale = subGradeScaleBestFirst();
    for (size_t i = 1; i < scale.size(); ++i) {int x, y; subGradeRank(scale[i - 1], x); subGradeRank(scale[i], y); check(x == y + 1, "scale steps by one");}
    // overall letter: A best, Z worst
    check(letterRank("A", a) && letterRank("B", b) && a > b, "A above B");
    check(letterRank("Z", a) && a == 0, "Z is the lowest");
    check(!letterRank(" ", a) && !letterRank("", a), "no letter");
    // range text
    std::string lo, hi;
    check(splitRange("b to A", lo, hi) && lo == "b" && hi == "A", "b to A");
    check(splitRange("b-A", lo, hi) && lo == "b" && hi == "A", "b-A");
    check(splitRange("1 to 40", lo, hi) && lo == "1" && hi == "40", "1 to 40");
    check(splitRange("1-40", lo, hi) && lo == "1" && hi == "40", "1-40");
    check(!splitRange("40", lo, hi), "one end only");
    // old saved filters held a number for the position in the newest scale
    check(legacySubGradeLetter("authority", 0) == "p" && legacySubGradeLetter("authority", 15) == "a" && legacySubGradeLetter("authority", 16) == "A", "legacy authority");
    check(legacySubGradeLetter("power", 0) == "q" && legacySubGradeLetter("detail", 0) == "t" && legacySubGradeLetter("detail", 32) == "M", "legacy power / detail");
    check(legacySubGradeLetter("detail", 33).empty() && legacySubGradeLetter("power", -1).empty(), "legacy out of range");
    // sorting grades by their text, like a Finder column (best first)
    check(compareGradeText("1-AAAA", "1-AABA") < 0 && compareGradeText("1-AABA", "1-BDDD") < 0, "1-AAAA above 1-AABA above 1-BDDD");
    check(compareGradeText("2-AAAA", "10-AAAA") < 0, "2- above 10- (numbers as numbers)");
    check(compareGradeText("1-BDDD", "AAAA") < 0, "numbered above unnumbered");
    check(compareGradeText("95-Abbd", "AAAA") < 0 && compareGradeText("80-Abbd", "95-Abbd") < 0, "0.5.8 numbers above 80 still count as numbers");
    check(gradeNumber("95-Abbd") == 95 && gradeNumber(" 7-AJJK") == 7 && gradeNumber("Abbd") == 0 && gradeNumber("") == 0 && gradeNumber("12") == 0, "gradeNumber reads the shown number only");
    check(compareGradeText("1-AAAA", "1-AAAa") < 0 && compareGradeText("1-AAAa", "1-AAAA") > 0, "capital above lowercase when letters are equal");
    check(compareGradeText("35-BaaB", "1-AAAA") > 0, "35- below 1-");
    check(compareGradeText("BaaB", "BaaB") == 0, "equal");
    check(compareGradeTextDir("", "AAAA", true) > 0 && compareGradeTextDir("", "AAAA", false) > 0 && compareGradeTextDir("AAAA", "", false) < 0, "ungraded last both ways");
    check(compareGradeTextDir("1-AAAA", "1-AABA", false) > 0, "reverse is backwards");
    printf("grade rank test: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

int main()
{
    const double sr = 44100.0; const double tau = 2.0*M_PI;
    std::mt19937 rng(1); std::normal_distribution<float> nd(0.0f, 1.0f);
    runSignal("sine 50 Hz -6 dB", sr, 20, [&](long i, float& l, float& r){ l = r = 0.5f*(float)std::sin(tau*50*i/sr); }, true);
    runSignal("sine 1 kHz -20 dBFS", sr, 20, [&](long i, float& l, float& r){ l = r = 0.1f*(float)std::sin(tau*1000*i/sr); }, true);
    runSignal("sine 2 kHz -6 dB", sr, 20, [&](long i, float& l, float& r){ l = r = 0.5f*(float)std::sin(tau*2000*i/sr); }, true);
    runSignal("pink-ish noise", sr, 20, [&](long, float& l, float& r){ static float b = 0; b = 0.98f*b + 0.02f*nd(rng); l = 0.6f*b*8; r = 0.6f*b*8 + 0.05f*nd(rng); }, true);
    runSignal("white noise -12 dB", sr, 20, [&](long, float& l, float& r){ l = 0.25f*nd(rng); r = 0.25f*nd(rng); }, true);
    runSignal("drum-ish bursts", sr, 20, [&](long i, float& l, float& r){ const double t = std::fmod(i/sr, 0.5); l = r = (float)(0.9*std::exp(-t*30)*std::sin(tau*(60 + 200*std::exp(-t*40))*t)); }, true);
    runSignal("sine 1 kHz @48k", 48000.0, 10, [&](long i, float& l, float& r){ l = r = 0.5f*(float)std::sin(tau*1000*i/48000.0); }, false);
    // 1 kHz sine at -20 dBFS peak: true peak should be about -20 dB, LUFS about -23.0 (a -20 dBFS peak sine is -23 dBFS RMS, K-weighting adds ~0)
    // tempo and key on simple synthetic songs (kick on the beat, hats between, a pad chord and a bass note)
    int estFails = 0;
    struct T {double bpm; int root; bool minor; const char* key;} tests[] = {{128, 9, true, "A minor"}, {95, 0, false, "C major"}, {120, 7, false, "G major"}, {140, 2, true, "D minor"}};
    std::uniform_real_distribution<float> u(-1, 1);
    for (auto t : tests) {
        Capture cap(sr); std::vector<float> L(4096); const long total = (long)(30*sr);
        const int third = t.minor ? 3 : 4; const double f0 = 220.0*std::pow(2.0, (t.root - 9)/12.0);
        for (long pos = 0; pos < total; pos += 4096) {
            const int n = (int)std::min<long>(4096, total - pos);
            for (int i = 0; i < n; ++i) {
                const double tt = (pos + i)/sr, beat = 60.0/t.bpm, tb = std::fmod(tt, beat), th = std::fmod(tt + beat/2, beat);
                double x = 0.8*std::exp(-tb*25)*std::sin(tau*(50 + 100*std::exp(-tb*30))*tb) + 0.15*std::exp(-th*80)*u(rng) + 0.2*std::sin(tau*f0/2*tt);
                for (int st : {0, third, 7}) x += 0.08*std::sin(tau*f0*std::pow(2.0, st/12.0)*tt) + 0.04*std::sin(tau*2*f0*std::pow(2.0, st/12.0)*tt);
                L[i] = (float)x;
            }
            cap.process(L.data(), L.data(), n);
        }
        cap.finish();
        const auto b = estimateBpm(cap.fine, cap.fineBlockSamples/sr); const auto k = estimateKey(cap.pitch.frames);
        const bool ok = std::fabs(b.bpm - t.bpm) < 1.0 && k.name == t.key;
        if (!ok) ++estFails;
        printf("song %5.1f BPM %-8s -> %5.1f BPM (conf %.2f), %s (conf %.3f)  %s\n", t.bpm, t.key, b.bpm, b.confidence, k.name.c_str(), k.confidence, ok ? "ok" : "WRONG");
    }
    const bool rateOk = rateInvariance();
    if (!gradeRankTest()) return 1;
    printf("worst merge error %g\n", gWorst);
    if (!rateOk) {printf("RATE INVARIANCE FAILED\n"); return 1;}
    if (estFails) return 1;
    return gWorst < 1e-5 ? 0 : 1;
}
