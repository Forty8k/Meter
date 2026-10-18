// A streaming sample rate converter (polyphase, Kaiser-windowed sinc). Pure C++, no JUCE, so the plugin and the analyser use the SAME file
// (keep analyser/src/Resampler.h and source/Resampler.h identical) and a track scores the same in both.
// Passband flat to 0.907 of the lower Nyquist (20 kHz at 44.1 kHz), stopband from the lower Nyquist, at least 100 dB down. Mono per instance.
// In and out rates must reduce to a ratio L/M with L <= 1024 (every common rate does: 44.1, 48, 88.2, 96, 176.4, 192 ...).
#pragma once
#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace f8kdsp {

class Resampler {
public:
    Resampler() = default;
    Resampler(double inRate, double outRate) { setRates(inRate, outRate); }

    bool ok() const { return valid; }
    bool isPassThrough() const { return pass; }
    int latencyInputSamples() const { return 0; } // the output is already lined up with the input (output sample m is the input at time m*in/out); the filter's own delay is absorbed
    double inRate() const { return in; }
    double outRate() const { return out; }

    void setRates(double inRate, double outRate)
    {
        in = inRate; out = outRate; valid = false; pass = false;
        history.clear(); table.clear(); phasePos = 0; baseIndex = 0; total = 0;
        const int64_t a = (int64_t)std::llround(inRate), b = (int64_t)std::llround(outRate);
        if (a <= 0 || b <= 0) return;
        if (a == b) { pass = true; valid = true; return; }
        const int64_t g = gcd(a, b);
        L = (int)(b/g); M = (int)(a/g);
        if (L > 1024) return;
        // prototype lowpass at rate in*L: cut between pass and stop edges (in Hz), gain L so the passband is 1
        const double nyq = 0.5*std::min(inRate, outRate);
        const double passEdge = 0.907*nyq, stopEdge = nyq, atten = 100.0;
        const double fsProto = inRate*(double)L;
        const double dw = 2.0*M_PI_*(stopEdge - passEdge)/fsProto;
        int n = (int)std::ceil((atten - 7.95)/(2.285*dw)) + 1;
        taps = (n + L - 1)/L; if (taps < 8) taps = 8; taps = (taps + 7)/8*8; // taps per phase, a multiple of 8 (the dot product runs in 8 lanes)
        const int len = taps*L + 1; // odd, so the centre of the prototype sits exactly on a sample (no half-sample offset)
        const double beta = 0.1102*(atten - 8.7);
        const double fc = 0.5*(passEdge + stopEdge)/fsProto; // cycles per proto sample
        const double mid = 0.5*(double)(len - 1);
        table.assign((size_t)(taps*L), 0.0f);
        const double i0b = bessel0(beta);
        double sum = 0.0;
        std::vector<double> h((size_t)len);
        for (int i = 0; i < len; ++i) {
            const double x = (double)i - mid;
            const double s = (x == 0.0) ? 2.0*fc : std::sin(2.0*M_PI_*fc*x)/(M_PI_*x);
            const double r = x/mid; const double w = bessel0(beta*std::sqrt(std::max(0.0, 1.0 - r*r)))/i0b;
            h[(size_t)i] = s*w; sum += h[(size_t)i];
        }
        const double norm = (double)L/sum; // DC gain of each phase = 1 on average
        // phase p, tap k uses h[p + k*L]; store as table[p*taps + k] so a phase is contiguous
        // stored oldest-first, so the multiply-add runs forward along both arrays (and vectorises)
        for (int p = 0; p < L; ++p) for (int k = 0; k < taps; ++k) table[(size_t)p*(size_t)taps + (size_t)(taps - 1 - k)] = (float)(h[(size_t)(p + k*L)]*norm);
        history.assign((size_t)taps, 0.0f); // the last `taps` input samples, newest at the end
        phasePos = (int64_t)(taps/2)*L;     // start taps/2 input samples ahead, so the filter's centre sits on the output instant: no delay
        valid = true;
    }

    // Converts n input samples. Output samples are appended to `outv`. Returns how many were appended.
    size_t process(const float* x, size_t n, std::vector<float>& outv)
    {
        if (!valid) return 0;
        if (pass) { outv.insert(outv.end(), x, x + n); return n; }
        const size_t before = outv.size();
        // work on history (taps-1 old samples) + new input, so every output can look back `taps` samples
        work.assign(history.begin(), history.end()); work.insert(work.end(), x, x + n);
        // output m corresponds to input position pos = m*M/L (in input samples); it needs inputs up to floor(pos) and `taps` back.
        // `phasePos` = position of the next output, in units of 1/L input sample, relative to the start of `work` + (taps-1).
        const int64_t avail = (int64_t)n; // new samples
        for (;;) {
            const int64_t whole = phasePos / L;          // index of newest input sample used, relative to the first NEW sample
            if (whole >= avail) break;                    // not enough input yet
            const int p = (int)(phasePos % L);
            const float* c = &table[(size_t)p*(size_t)taps];
            // newest input sample is work[(taps) + whole] (history holds taps old samples first); take taps samples ending there
            const float* s = &work[(size_t)(taps + whole - (taps - 1))];
            float lane[8] = {0, 0, 0, 0, 0, 0, 0, 0}; //eight independent sums, so the compiler can use SIMD without fast-math
            for (int k = 0; k < taps; k += 8) for (int j = 0; j < 8; ++j) lane[j] += c[k + j]*s[k + j];
            outv.push_back(((lane[0] + lane[4]) + (lane[1] + lane[5])) + ((lane[2] + lane[6]) + (lane[3] + lane[7])));
            phasePos += M;
        }
        phasePos -= (int64_t)avail*L;
        // keep the last `taps` input samples
        history.assign(work.end() - taps, work.end());
        total += (int64_t)n;
        return outv.size() - before;
    }

    // Pushes the filter's tail through with silence, so the last real samples appear in the output.
    size_t flush(std::vector<float>& outv)
    {
        if (!valid || pass) return 0;
        std::vector<float> z((size_t)taps, 0.0f);
        return process(z.data(), z.size(), outv);
    }

private:
    static constexpr double M_PI_ = 3.14159265358979323846;
    static int64_t gcd(int64_t a, int64_t b) { while (b) { const int64_t t = a % b; a = b; b = t; } return a; }
    static double bessel0(double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 60; ++k) { t *= (x/(2.0*k))*(x/(2.0*k)); s += t; if (t < 1e-14*s) break; } return s; }
    double in = 0, out = 0;
    bool valid = false, pass = false;
    int L = 1, M = 1, taps = 8;
    std::vector<float> table, history, work;
    int64_t phasePos = 0, baseIndex = 0, total = 0;
};

} // namespace f8kdsp
