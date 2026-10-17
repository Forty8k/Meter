// Self-test: (1) rebuilding the core windows from fine blocks matches the plugin's own windows,
// (2) the original scorer runs and gives sensible results on test tones. Build: see tools/run_selftest.sh
#include "../src/Capture.h"
#include "../src/OriginalScorer.h"
#include <cstdio>
#include <random>
#include <functional>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace f8k;

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
    printf("worst merge error %g\n", gWorst);
    if (estFails) return 1;
    return gWorst < 1e-5 ? 0 : 1;
}
