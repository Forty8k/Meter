#include "Estimates.h"
#include "Capture.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace f8k {

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0*M_PI/(double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len/2; ++k) {
                const auto u = a[i+k], v = a[i+k+len/2]*w;
                a[i+k] = u + v; a[i+k+len/2] = u - v;
                w *= wl;
            }
        }
    }
}

PitchCapture::PitchCapture(double sr) : sampleRate(sr)
{
    fftSize = sampleRate > 70000.0 ? 16384 : (sampleRate < 30000.0 ? 4096 : 8192); //about 93-170 ms at any rate
    hop = fftSize/2;
    ring.assign(fftSize, 0.0);
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i) window[i] = 0.5 - 0.5*std::cos(2.0*M_PI*i/(fftSize - 1));
    buf.resize(fftSize);
    binClass.assign(fftSize/2, -1);
    for (int k = 1; k < fftSize/2; ++k) {
        const double f = k*sampleRate/fftSize;
        if (f < 55.0 || f > 5000.0) continue;
        const double midi = 69.0 + 12.0*std::log2(f/440.0);
        binClass[k] = ((int)std::lround(midi) % 12 + 12) % 12; // C = 0
    }
}

void PitchCapture::push(double mid)
{
    ring[fill % fftSize] = mid;
    ++fill;
    if (fill < fftSize) return;
    if (fill == fftSize || ++sinceLast >= hop) {sinceLast = 0; frame();}
}

void PitchCapture::frame()
{
    const int start = fill % fftSize; //oldest sample
    double sq = 0;
    for (int i = 0; i < fftSize; ++i) {
        const double x = ring[(start + i) % fftSize];
        sq += x*x;
        buf[i] = std::complex<double>(x*window[i], 0.0);
    }
    fft(buf);
    float row[kPitchCols] = {};
    for (int k = 1; k < fftSize/2; ++k) if (binClass[k] >= 0) row[binClass[k]] += (float)std::abs(buf[k]);
    row[12] = (float)std::sqrt(sq/fftSize);
    frames.insert(frames.end(), row, row + kPitchCols);
}

KeyEstimate estimateKey(const std::vector<float>& pf)
{
    static const double major[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    static const char* names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    KeyEstimate k;
    const size_t n = pf.size()/kPitchCols;
    if (n == 0) return k;
    double maxRms = 0;
    for (size_t i = 0; i < n; ++i) maxRms = std::max(maxRms, (double)pf[i*kPitchCols + 12]);
    double chroma[12] = {};
    for (size_t i = 0; i < n; ++i) {
        const float* r = &pf[i*kPitchCols];
        if (r[12] < maxRms*0.03) continue; //skip near-silence
        double s = 0; for (int c = 0; c < 12; ++c) s += r[c];
        if (s <= 0) continue;
        for (int c = 0; c < 12; ++c) chroma[c] += r[c]/s * r[12]; //each frame's note shape, weighted by its level
    }
    auto corr = [](const double* a, const double* b) {
        double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) {ma += a[i]; mb += b[i];}
        ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i) {num += (a[i]-ma)*(b[i]-mb); da += (a[i]-ma)*(a[i]-ma); db += (b[i]-mb)*(b[i]-mb);}
        return (da > 0 && db > 0) ? num/std::sqrt(da*db) : 0.0;
    };
    double best = -2, second = -2;
    for (int t = 0; t < 12; ++t) for (int m = 0; m < 2; ++m) {
        double prof[12];
        for (int i = 0; i < 12; ++i) prof[(i + t) % 12] = m ? minor[i] : major[i];
        const double c = corr(chroma, prof);
        if (c > best) {second = best; best = c; k.tonic = t; k.minor = m == 1;}
        else if (c > second) second = c;
    }
    if (k.tonic < 0 || best <= 0) {k.tonic = -1; return k;}
    k.name = std::string(names[k.tonic]) + (k.minor ? " minor" : " major");
    k.confidence = best - second; //how clearly the best key beats the next one (0 = a toss-up)
    return k;
}

BpmEstimate estimateBpm(const std::vector<float>& fine, double blockSec)
{
    BpmEstimate out;
    const size_t rows = fine.size()/F_NUM_COLS;
    if (rows < 400 || blockSec <= 0) return out;
    //onset strength: rises in the log energy of each octave band, summed (spectral flux on ten bands).
    //Each band's energy is first averaged over ~46 ms, so the ripple of a steady low note does not look like a stream of onsets.
    int avgNb[10]; //at least ~46 ms, and at least 4 cycles of the band's centre for the lowest bands
    for (int b = 0; b < 10; ++b) avgNb[b] = std::max(1, (int)std::lround(std::max(0.046, 4.0/kBandCentres[b])/blockSec));
    std::vector<double> env(rows, 0.0);
    {
        static const double bandWeight[10] = {1.0, 1.0, 0.8, 0.6, 0.5, 0.4, 0.35, 0.3, 0.3, 0.3}; //kick and bass carry the beat more than hats
        std::vector<double> run(10, 0.0), prev(10, 0.0);
        double meanAll = 0; //a floor tied to the track's own level, so near-silent bands do not flicker in log terms
        for (size_t r = 0; r < rows; ++r) for (int b = 0; b < 10; ++b) meanAll += fine[r*F_NUM_COLS + F_BAND0 + b];
        const double floorE = std::max(1e-12, 1e-3*meanAll/(double)rows);
        for (size_t r = 0; r < rows; ++r) {
            double o = 0;
            for (int b = 0; b < 10; ++b) {
                const int avgN = avgNb[b];
                run[b] += fine[r*F_NUM_COLS + F_BAND0 + b];
                if (r >= (size_t)avgN) run[b] -= fine[(r - avgN)*F_NUM_COLS + F_BAND0 + b];
                const double e = std::log(floorE + std::max(0.0, run[b])/avgN);
                if (r > (size_t)avgNb[0]) o += bandWeight[b]*std::max(0.0, e - prev[b]);
                prev[b] = e;
            }
            env[r] = o;
        }
    }
    { double m = 0; for (double v : env) m += v; m /= (double)rows; if (m < 0.02) return out; } //no onsets to speak of (silence, a steady tone): no tempo
    //remove the slow trend so only the pulses are left
    const int smooth = std::max(1, (int)std::lround(0.4/blockSec));
    std::vector<double> csum(rows + 1, 0.0);
    for (size_t i = 0; i < rows; ++i) csum[i+1] = csum[i] + env[i];
    for (size_t i = 0; i < rows; ++i) {
        const size_t a = i >= (size_t)smooth ? i - smooth : 0, b = std::min(rows, i + smooth + 1);
        env[i] = std::max(0.0, env[i] - (csum[b] - csum[a])/(double)(b - a));
    }
    { //soften each pulse over ~20 ms so beats a fraction of a block apart still line up
        const int R = std::max(1, (int)std::lround(0.012/blockSec));
        std::vector<double> sm(rows, 0.0), w(2*R + 1);
        for (int k = -R; k <= R; ++k) w[k + R] = std::exp(-0.5*(k*k)/(0.5*R*R));
        for (size_t i = 0; i < rows; ++i) {
            double s = 0;
            for (int k = -R; k <= R; ++k) {const long j = (long)i + k; if (j >= 0 && j < (long)rows) s += env[j]*w[k + R];}
            sm[i] = s;
        }
        env.swap(sm);
    }
    { const double m = std::accumulate(env.begin(), env.end(), 0.0)/(double)rows; for (auto& v : env) v -= m; } //only the pulses, not their average
    //autocorrelation over the lags of 40-240 BPM (and their multiples, for the comb below)
    const int maxLag = (int)std::ceil(60.0/(40.0*blockSec))*4 + 2;
    std::vector<double> ac(maxLag + 1, 0.0);
    for (int L = 1; L <= maxLag; ++L) {
        double s = 0;
        for (size_t i = L; i < rows; ++i) s += env[i]*env[i-L];
        ac[L] = s/(double)(rows - L);
    }
    auto acAt = [&](double lag) {
        if (lag < 1 || lag >= maxLag) return 0.0;
        const int i = (int)lag; const double f = lag - i;
        return ac[i]*(1-f) + ac[i+1]*f;
    };
    double ac0 = 0; for (size_t i = 0; i < rows; ++i) ac0 += env[i]*env[i]; ac0 /= (double)rows;
    double best = -1e300, bestBpm = 0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.05) {
        const double lag = 60.0/(bpm*blockSec);
        double s = 0;
        for (int k = 1; k <= 4; ++k) s += acAt(lag*k)/k; //beats line up at every multiple of the beat period
        s += 0.5*acAt(lag*0.5); //and most music splits the beat in two (this keeps 174 from reading as 116)
        const double prior = std::exp(-0.5*std::pow(std::log2(bpm/120.0)/0.9, 2.0)); //gently prefer the usual tempo range
        s *= prior;
        if (s > best) {best = s; bestBpm = bpm;}
    }
    out.bpm = std::round(bestBpm*10.0)/10.0;
    out.confidence = ac0 > 0 ? std::clamp(acAt(60.0/(bestBpm*blockSec))/ac0, 0.0, 1.0) : 0; //how regular the pulse is at that tempo (0 = no clear beat, 1 = a metronome)
    return out;
}

} // namespace f8k
