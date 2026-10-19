// meterDARK Analyser: the measuring part. Pure C++, no JUCE, so it can be tested on its own.
//
// Two layers are captured from the same audio:
//  CORE  - the ten numbers per audio window that the original Airwindows Meter 0.5.5 plugin sends to its display,
//          worked out with the plugin's own sample loop (copied from PluginProcessor::processBlock), so they match it exactly.
//  FINE  - short blocks (about 5.8 ms) holding enough raw detail to rebuild any longer window later (peaks, slews,
//          zero-cross positions, beam and departure combine exactly), plus extras the original never uses:
//          RMS, DC, true peak, K-weighted loudness energy, stereo correlation and mid/side, and ten octave bands.
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include "Estimates.h"

namespace mdark {

constexpr const char* kCaptureVersion = "1.2"; // 1.1 added the pitch layer; 1.2 added the 44.1 kHz / 96 kHz runs of every Meter version

// One CORE window, exactly as the original plugin sends it (peak is already square-rooted, as in the plugin).
struct CoreWindow { float peakL, peakR, slewL, slewR, zeroL, zeroR, beamL, beamR, deptL, deptR; };

// The FINE block columns, in file order. Index names are used by the writer and the Python reader.
enum FineCol : int {
    F_N = 0,              // samples in this block (the last block can be short)
    // per channel, L then R (add F_CH_STRIDE for R)
    F_PEAK = 1,           // max |x|
    F_MAX, F_MIN,         // signed max and min
    F_MS,                 // mean square (RMS = sqrt)
    F_DC,                 // mean
    F_SLEW,               // max slew, the plugin's formula: |x - previous| / 32000 * sampleRate
    F_ZC_COUNT,           // zero crossings in this block, the plugin's definition (sign changes, exact zeros do not flip)
    F_ZC_FIRST,           // index in the block of the first crossing (-1 = none)
    F_ZC_LAST,            // index in the block of the last crossing (-1 = none)
    F_ZC_GAPMAX,          // longest gap between two crossings inside the block, in samples (0 if fewer than two)
    F_DEPT_LOCAL,         // the plugin's departure, run over this block only starting from 0 (windows combine these exactly)
    F_BEAM_END,           // the plugin's beam value at the end of the block (it runs continuously)
    F_KW_MS,              // K-weighted mean square (ITU-R BS.1770), for loudness in LUFS
    F_TRUEPEAK,           // 4x oversampled peak (approximate true peak)
    F_CH_END,
    F_CH_STRIDE = F_CH_END - F_PEAK,
    F_LR = F_PEAK + 2*F_CH_STRIDE, // mean of L*R (correlation numerator)
    F_MID_MS,             // mean square of (L+R)/2
    F_SIDE_MS,            // mean square of (L-R)/2
    F_BAND0,              // ten octave bands of the mid signal, mean square: centres 31.25, 62.5, 125 ... 16000 Hz
    F_NUM_COLS = F_BAND0 + 10
};
std::vector<std::string> fineColumnNames();
constexpr double kBandCentres[10] = {31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0/A0; b1 = B1/A0; b2 = B2/A0; a1 = A1/A0; a2 = A2/A0; }
    inline double process(double x) { const double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return y; }
};

class Capture {
public:
    explicit Capture(double sampleRate, int fineBlockOverride = 0); // override is for self-tests only
    void process(const float* L, const float* R, int numSamples); // call repeatedly with consecutive chunks
    void finish();                                               // flushes the last partial block (a partial core window is dropped, as the plugin never sends it)

    double sampleRate;
    int coreWindowSamples = 0;  // samples per core window (the plugin's: floor(1881/44100*sr)+1)
    int fineBlockSamples = 0;   // samples per fine block (about 256 at 44.1 kHz)
    std::vector<CoreWindow> core;
    std::vector<float> fine;    // row-major, F_NUM_COLS per block
    size_t fineRows() const { return fine.size() / F_NUM_COLS; }
    PitchCapture pitch;         // the pitch layer (see Estimates.h)

    // track summary
    double peakAbs = 0.0, truePeakAbs = 0.0;
    double integratedLufs() const;       // BS.1770 gated integrated loudness (-inf if silent)
    long long totalSamples = 0;

private:
    // ---- core state: same names and maths as the original PluginProcessor ----
    double windowSize, zeroCrossScale, beamScale;
    double peakLeft = 0, peakRight = 0, slewLeft = 0, slewRight = 0, previousLeft = 0, previousRight = 0;
    double zeroLeft = 0, zeroRight = 0, longestZeroLeft = 0, longestZeroRight = 0;
    double beamLeft = 0, beamRight = 0, departureLeft = 0, departureRight = 0;
    bool wasPositiveL = false, wasPositiveR = false;
    int windowCount = 0;

    // ---- fine state ----
    struct Ch {
        double prev = 0, beam = 0; bool wasPos = false;
        Biquad kw1, kw2;
        double hist[16] = {};
        // block accumulators
        double peak, mx, mn, sq, sum, slew, dept, kw, tp; int zcCount, zcFirst, zcLast, zcGap, lastCross;
        void resetBlock() { peak = 0; mx = -1e30; mn = 1e30; sq = 0; sum = 0; slew = 0; dept = 0; kw = 0; tp = 0; zcCount = 0; zcFirst = -1; zcLast = -1; zcGap = 0; lastCross = -1; }
    } ch[2];
    Biquad band[10];
    bool bandOn[10] = {};
    double lr = 0, midSq = 0, sideSq = 0, bandSq[10] = {};
    int blockPos = 0;
    double interp[3][16] = {}; // true-peak interpolation kernels for t = .25, .5, .75
    // loudness: K-weighted energy per 100 ms hop, summed over both channels
    int hopSamples = 0, hopPos = 0; double hopEnergy = 0; std::vector<double> hops;

    void sampleCore(double inputSampleL, double inputSampleR);
    void sampleFine(int c, double x);
    void endBlock();
};

} // namespace mdark
