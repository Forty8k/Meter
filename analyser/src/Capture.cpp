#include "Capture.h"
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mdark {

std::vector<std::string> fineColumnNames()
{
    std::vector<std::string> n(F_NUM_COLS);
    n[F_N] = "n";
    const char* chNames[2] = {"L", "R"};
    const char* per[F_CH_STRIDE] = {"peak", "max", "min", "ms", "dc", "slew", "zc_count", "zc_first", "zc_last", "zc_gapmax", "dept_local", "beam_end", "kw_ms", "truepeak"};
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < F_CH_STRIDE; ++k) n[F_PEAK + c*F_CH_STRIDE + k] = std::string(per[k]) + "_" + chNames[c];
    n[F_LR] = "lr_mean"; n[F_MID_MS] = "mid_ms"; n[F_SIDE_MS] = "side_ms";
    for (int b = 0; b < 10; ++b) {
        const double f = kBandCentres[b];
        n[F_BAND0 + b] = "band_" + std::to_string((int)f) + "_ms";
    }
    return n;
}

static void setHighShelf(Biquad& q, double fs, double f0, double gainDb, double Q)
{
    const double A = std::pow(10.0, gainDb/40.0), w0 = 2.0*M_PI*f0/fs, cw = std::cos(w0), alpha = std::sin(w0)/(2.0*Q), sA = std::sqrt(A);
    q.set(A*((A+1) + (A-1)*cw + 2*sA*alpha), -2*A*((A-1) + (A+1)*cw), A*((A+1) + (A-1)*cw - 2*sA*alpha),
          (A+1) - (A-1)*cw + 2*sA*alpha, 2*((A-1) - (A+1)*cw), (A+1) - (A-1)*cw - 2*sA*alpha);
}
static void setHighPass(Biquad& q, double fs, double f0, double Q)
{
    const double w0 = 2.0*M_PI*f0/fs, cw = std::cos(w0), alpha = std::sin(w0)/(2.0*Q);
    q.set((1+cw)/2, -(1+cw), (1+cw)/2, 1+alpha, -2*cw, 1-alpha);
}
static void setBandPass(Biquad& q, double fs, double f0, double Q)
{
    const double w0 = 2.0*M_PI*f0/fs, cw = std::cos(w0), alpha = std::sin(w0)/(2.0*Q);
    q.set(alpha, 0.0, -alpha, 1+alpha, -2*cw, 1-alpha);
}

Capture::Capture(double sr, int fineBlockOverride) : sampleRate(sr), pitch(sr)
{
    //the plugin's per-window constants (PluginProcessor::processBlock)
    windowSize = (1881.0 / 44100.0)*sampleRate; //higher is slower with larger RMS buffers
    zeroCrossScale = (1.0 / sampleRate)*44100.0;
    beamScale = 1.0/sampleRate;
    coreWindowSamples = (int)std::floor(windowSize) + 1; //a window is sent once windowCount > windowSize
    fineBlockSamples = fineBlockOverride > 0 ? fineBlockOverride : std::max(1, (int)std::lround(256.0*sampleRate/44100.0));
    for (auto& c : ch) {
        setHighShelf(c.kw1, sampleRate, 1681.974450955533, 3.999843853973347, 0.7071752369554196); //BS.1770 K-weighting, stage 1
        setHighPass(c.kw2, sampleRate, 38.13547087602444, 0.5003270373238773);                  //stage 2
        c.resetBlock();
    }
    for (int b = 0; b < 10; ++b) {
        bandOn[b] = kBandCentres[b]*1.414 < sampleRate*0.49;
        if (bandOn[b]) setBandPass(band[b], sampleRate, kBandCentres[b], 1.41421356);
    }
    for (int p = 0; p < 3; ++p) { //Hann-windowed sinc, 16 taps, interpolating between hist[7] and hist[8]
        const double t = 0.25*(p+1);
        for (int k = 0; k < 16; ++k) {
            const double x = (7.0 + t) - (double)k;
            const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(M_PI*x)/(M_PI*x);
            const double w = 0.5 + 0.5*std::cos(M_PI*x/8.5);
            interp[p][k] = sinc*w;
        }
    }
    hopSamples = std::max(1, (int)std::lround(0.1*sampleRate));
}

void Capture::sampleCore(double inputSampleL, double inputSampleR)
{
    //---- copied from the original PluginProcessor::processBlock (Airwindows Meter 0.5.5), host and output lines removed ----
    double currentslewL = (fabs(inputSampleL-previousLeft)/32000.0)*sampleRate;
    double currentslewR = (fabs(inputSampleR-previousRight)/32000.0)*sampleRate;
    if (currentslewL > slewLeft) slewLeft = currentslewL;
    if (currentslewR > slewRight) slewRight = currentslewR;
    previousLeft = inputSampleL;
    previousRight = inputSampleR;

    if (fabs(beamLeft-currentslewL) < 0.2) beamLeft = (beamLeft*(1.0-beamScale)) + (currentslewL*beamScale);
    else beamLeft = (beamLeft*(1.0-beamScale));
    if (fabs(beamRight-currentslewR) < 0.2) beamRight = (beamRight*(1.0-beamScale)) + (currentslewR*beamScale);
    else beamRight = (beamRight*(1.0-beamScale));
    departureLeft = (departureLeft*(1.0-beamScale)) + (fabs(beamLeft-currentslewL)*beamScale);
    departureRight = (departureRight*(1.0-beamScale)) + (fabs(beamRight-currentslewR)*beamScale);

    double rectifiedL = fabs(inputSampleL);
    double rectifiedR = fabs(inputSampleR);
    if (rectifiedL > peakLeft) peakLeft = rectifiedL;
    if (rectifiedR > peakRight) peakRight = rectifiedR;

    zeroLeft += zeroCrossScale;
    if (longestZeroLeft < zeroLeft) longestZeroLeft = zeroLeft;
    if (wasPositiveL && inputSampleL < 0.0) {
        wasPositiveL = false;
        zeroLeft = 0.0;
    } else if (!wasPositiveL && inputSampleL > 0.0) {
        wasPositiveL = true;
        zeroLeft = 0.0;
    }
    zeroRight += zeroCrossScale;
    if (longestZeroRight < zeroRight) longestZeroRight = zeroRight;
    if (wasPositiveR && inputSampleR < 0.0) {
        wasPositiveR = false;
        zeroRight = 0.0;
    } else if (!wasPositiveR && inputSampleR > 0.0) {
        wasPositiveR = true;
        zeroRight = 0.0;
    }
    windowCount++;

    if (windowCount > windowSize)
    {
        CoreWindow w; //the same values, in the same float form, as the plugin's AudioToUIMessages
        w.peakL = (float)sqrt(peakLeft); w.peakR = (float)sqrt(peakRight);
        w.slewL = (float)slewLeft; w.slewR = (float)slewRight;
        w.zeroL = (float)longestZeroLeft; w.zeroR = (float)longestZeroRight;
        w.beamL = (float)beamLeft; w.beamR = (float)beamRight;
        w.deptL = (float)departureLeft; w.deptR = (float)departureRight;
        core.push_back(w);
        peakLeft = 0.0;
        peakRight = 0.0;
        slewLeft = 0.0;
        slewRight = 0.0; //reset our variables to do the RMS again
        zeroLeft = 0.0;
        zeroRight = 0.0;
        departureLeft = 0.0;
        departureRight = 0.0;
        longestZeroLeft = 0.0;
        longestZeroRight = 0.0;
        windowCount = 0;
    }
}

void Capture::sampleFine(int c, double x)
{
    Ch& s = ch[c];
    const double a = beamScale;
    //slew, beam and departure exactly as the plugin, but departure starts from 0 at each block (windows sum these back up)
    const double currentslew = (fabs(x - s.prev)/32000.0)*sampleRate;
    s.prev = x;
    if (currentslew > s.slew) s.slew = currentslew;
    if (fabs(s.beam - currentslew) < 0.2) s.beam = (s.beam*(1.0-a)) + (currentslew*a);
    else s.beam = (s.beam*(1.0-a));
    s.dept = (s.dept*(1.0-a)) + (fabs(s.beam - currentslew)*a);
    const double ax = fabs(x);
    if (ax > s.peak) s.peak = ax;
    if (x > s.mx) s.mx = x;
    if (x < s.mn) s.mn = x;
    s.sq += x*x; s.sum += x;
    //zero crossings, the plugin's definition
    bool crossed = false;
    if (s.wasPos && x < 0.0) {s.wasPos = false; crossed = true;}
    else if (!s.wasPos && x > 0.0) {s.wasPos = true; crossed = true;}
    if (crossed) {
        if (s.lastCross >= 0) s.zcGap = std::max(s.zcGap, blockPos - s.lastCross);
        if (s.zcFirst < 0) s.zcFirst = blockPos;
        s.lastCross = blockPos; s.zcLast = blockPos; s.zcCount++;
    }
    //loudness
    const double k = s.kw2.process(s.kw1.process(x));
    s.kw += k*k; hopEnergy += k*k;
    //true peak
    for (int i = 0; i < 15; ++i) s.hist[i] = s.hist[i+1];
    s.hist[15] = x;
    double tp = ax;
    for (int p = 0; p < 3; ++p) {
        double y = 0.0;
        for (int i = 0; i < 16; ++i) y += interp[p][i]*s.hist[i];
        tp = std::max(tp, fabs(y));
    }
    if (tp > s.tp) s.tp = tp;
}

void Capture::process(const float* L, const float* R, int numSamples)
{
    for (int i = 0; i < numSamples; ++i) {
        const double inputSampleL = L[i]; //the plugin reads float input into doubles
        const double inputSampleR = R[i];
        sampleCore(inputSampleL, inputSampleR);
        sampleFine(0, inputSampleL);
        sampleFine(1, inputSampleR);
        lr += inputSampleL*inputSampleR;
        const double mid = 0.5*(inputSampleL + inputSampleR), side = 0.5*(inputSampleL - inputSampleR);
        midSq += mid*mid; sideSq += side*side;
        pitch.push(mid);
        for (int b = 0; b < 10; ++b) if (bandOn[b]) {const double y = band[b].process(mid); bandSq[b] += y*y;}
        peakAbs = std::max(peakAbs, std::max(fabs(inputSampleL), fabs(inputSampleR)));
        totalSamples++;
        blockPos++;
        if (blockPos >= fineBlockSamples) endBlock();
        if (++hopPos >= hopSamples) {hops.push_back(hopEnergy/hopSamples); hopEnergy = 0; hopPos = 0;}
    }
}

void Capture::endBlock()
{
    if (blockPos <= 0) return;
    const double n = (double)blockPos;
    float row[F_NUM_COLS] = {};
    row[F_N] = (float)blockPos;
    for (int c = 0; c < 2; ++c) {
        Ch& s = ch[c];
        float* r = row + F_PEAK + c*F_CH_STRIDE;
        r[0] = (float)s.peak; r[1] = (float)s.mx; r[2] = (float)s.mn; r[3] = (float)(s.sq/n); r[4] = (float)(s.sum/n);
        r[5] = (float)s.slew; r[6] = (float)s.zcCount; r[7] = (float)s.zcFirst; r[8] = (float)s.zcLast; r[9] = (float)s.zcGap;
        r[10] = (float)s.dept; r[11] = (float)s.beam; r[12] = (float)(s.kw/n); r[13] = (float)s.tp;
        truePeakAbs = std::max(truePeakAbs, s.tp);
        s.resetBlock();
    }
    row[F_LR] = (float)(lr/n); row[F_MID_MS] = (float)(midSq/n); row[F_SIDE_MS] = (float)(sideSq/n);
    for (int b = 0; b < 10; ++b) {row[F_BAND0 + b] = (float)(bandSq[b]/n); bandSq[b] = 0;}
    lr = 0; midSq = 0; sideSq = 0;
    fine.insert(fine.end(), row, row + F_NUM_COLS);
    blockPos = 0;
}

void Capture::finish()
{
    endBlock();
}

double Capture::integratedLufs() const
{
    //BS.1770: 400 ms blocks (4 hops) overlapping by 75%, absolute gate -70 LUFS, relative gate -10 LU
    std::vector<double> blocks;
    for (size_t i = 3; i < hops.size(); ++i) blocks.push_back(0.25*(hops[i] + hops[i-1] + hops[i-2] + hops[i-3]));
    auto lufs = [](double z) { return -0.691 + 10.0*std::log10(std::max(z, 1e-30)); };
    double sum = 0; int cnt = 0;
    for (double z : blocks) if (lufs(z) > -70.0) {sum += z; ++cnt;}
    if (cnt == 0) return -INFINITY;
    const double rel = lufs(sum/cnt) - 10.0;
    sum = 0; cnt = 0;
    for (double z : blocks) if (lufs(z) > -70.0 && lufs(z) > rel) {sum += z; ++cnt;}
    return cnt ? lufs(sum/cnt) : -INFINITY;
}

} // namespace mdark
