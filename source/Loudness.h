// Integrated loudness (LUFS) measurement, following ITU-R BS.1770 / EBU R128:
// K-weighting, 400 ms blocks every 100 ms, absolute gate at -70 LUFS and relative gate 10 LU below the result.
// No JUCE, no memory allocation while running, so it is safe to call from the audio thread.
// Used by the auto-trim: measure a section, then set the trim so that section reads the target loudness.
#pragma once
#include <cmath>
#include <array>

class LoudnessMeter
{
public:
    static constexpr float noReading = -1000.0f;

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        subBlockLength = (int)(sampleRate * 0.1 + 0.5); //100 ms
        makeFilters();
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s) {z1[c][s] = 0.0; z2[c][s] = 0.0;}
        subSum = 0.0;
        subCount = 0;
        subEnergy.fill(0.0);
        subsStored = 0;
        subIndex = 0;
        binEnergy.fill(0.0);
        binCount.fill(0);
    }

    //feed one stereo sample. Returns true when a new 400 ms block was added (so the reading may have changed)
    bool process(double left, double right)
    {
        if (subBlockLength < 1) return false;
        double y[2] = {left, right};
        for (int c = 0; c < 2; ++c) {
            for (int s = 0; s < 2; ++s) { //stage 0: high shelf, stage 1: high pass (together: K-weighting)
                const double x = y[c];
                const double out = b0[s]*x + z1[c][s];
                z1[c][s] = b1[s]*x - a1[s]*out + z2[c][s];
                z2[c][s] = b2[s]*x - a2[s]*out;
                y[c] = out;
            }
            subSum += y[c]*y[c]; //both channels weighted 1.0, summed
        }
        if (++subCount < subBlockLength) return false;

        subEnergy[subIndex] = subSum / (double)subCount; //mean square of this 100 ms, summed over the two channels
        subIndex = (subIndex + 1) % 4;
        if (subsStored < 4) ++subsStored;
        subSum = 0.0;
        subCount = 0;
        if (subsStored < 4) return false;

        const double blockEnergy = (subEnergy[0] + subEnergy[1] + subEnergy[2] + subEnergy[3]) * 0.25;
        if (blockEnergy <= 0.0) return true;
        const double blockLufs = -0.691 + 10.0 * std::log10(blockEnergy);
        if (blockLufs < gateStart) return true; //absolute gate: digital silence and near-silence never counts
        int bin = (int)((blockLufs - gateStart) / binSize);
        if (bin >= (int)binCount.size()) bin = (int)binCount.size() - 1;
        binEnergy[bin] += blockEnergy;
        binCount[bin] += 1;
        return true;
    }

    //integrated loudness of everything measured since reset(), or noReading if nothing has passed the gate yet
    float getIntegratedLufs() const
    {
        double totalEnergy = 0.0;
        long totalBlocks = 0;
        for (size_t i = 0; i < binCount.size(); ++i) {totalEnergy += binEnergy[i]; totalBlocks += binCount[i];}
        if (totalBlocks == 0) return noReading;
        const double relativeGate = (-0.691 + 10.0 * std::log10(totalEnergy / (double)totalBlocks)) - 10.0;
        double gatedEnergy = 0.0;
        long gatedBlocks = 0;
        for (size_t i = 0; i < binCount.size(); ++i) {
            const double binCentre = gateStart + ((double)i + 0.5) * binSize;
            if (binCount[i] > 0 && binCentre >= relativeGate) {gatedEnergy += binEnergy[i]; gatedBlocks += binCount[i];}
        }
        if (gatedBlocks == 0) return noReading;
        return (float)(-0.691 + 10.0 * std::log10(gatedEnergy / (double)gatedBlocks));
    }

    double getSampleRate() const {return sampleRate;}

private:
    //Filter coefficients for any sample rate (same derivation as libebur128), matching the BS.1770 numbers at 48 kHz.
    void makeFilters()
    {
        const double pi = 3.14159265358979323846;
        { //stage 0: high shelf, about +4 dB above 1.5 kHz, models the head
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(pi * f0 / sampleRate);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            b0[0] = (Vh + Vb * K / Q + K * K) / a0;
            b1[0] = 2.0 * (K * K - Vh) / a0;
            b2[0] = (Vh - Vb * K / Q + K * K) / a0;
            a1[0] = 2.0 * (K * K - 1.0) / a0;
            a2[0] = (1.0 - K / Q + K * K) / a0;
        }
        { //stage 1: revised low-frequency B-curve, a high pass near 38 Hz
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            b0[1] = 1.0;
            b1[1] = -2.0;
            b2[1] = 1.0;
            a1[1] = 2.0 * (K * K - 1.0) / a0;
            a2[1] = (1.0 - K / Q + K * K) / a0;
        }
    }

    static constexpr double gateStart = -70.0; //absolute gate, LUFS
    static constexpr double binSize = 0.1;     //LU per histogram bin
    static constexpr int binTotal = 800;       //covers -70 to +10 LUFS

    double sampleRate = 0.0;
    int subBlockLength = 0;
    double b0[2] = {1, 1}, b1[2] = {0, 0}, b2[2] = {0, 0}, a1[2] = {0, 0}, a2[2] = {0, 0};
    double z1[2][2] = {{0, 0}, {0, 0}}, z2[2][2] = {{0, 0}, {0, 0}};
    double subSum = 0.0;
    int subCount = 0;
    std::array<double, 4> subEnergy {};
    int subsStored = 0;
    int subIndex = 0;
    std::array<double, binTotal> binEnergy {};
    std::array<long, binTotal> binCount {};
};
