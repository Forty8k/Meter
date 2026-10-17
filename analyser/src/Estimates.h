// meterDARK Analyser: tempo and key estimates, and the PITCH layer they come from. Pure C++, no JUCE.
//  PITCH layer - every ~93 ms (8192-sample FFT, 50% overlap at 44.1/48 kHz): how strongly each of the 12 notes sounds
//                (C, C#, D ... B, summed over 55 Hz - 5 kHz), plus the frame's level. Kept in the library for later analysis.
//  BPM         - from the FINE blocks' octave bands (onsets), so it can be worked out for tracks scanned before this existed.
//  KEY         - the pitch layer against the Krumhansl-Kessler key profiles. "A minor" style names.
// Both are estimates: tempo can come out at half or double, key can land on the relative major/minor or a fifth away.
#pragma once
#include <vector>
#include <string>
#include <complex>

namespace f8k {

constexpr int kPitchCols = 13; // 12 note strengths (C first) + frame RMS

class PitchCapture {
public:
    explicit PitchCapture(double sampleRate);
    void push(double mid);           // one mono sample at a time
    void finish() {}                 // a trailing partial frame is dropped
    int fftSize = 8192, hop = 4096;
    double sampleRate;
    std::vector<float> frames;       // kPitchCols per frame
    size_t numFrames() const { return frames.size() / kPitchCols; }
private:
    std::vector<double> ring, window;
    std::vector<std::complex<double>> buf;
    std::vector<int> binClass; // pitch class for each FFT bin (-1 = outside 55 Hz - 5 kHz)
    int fill = 0, sinceLast = 0;
    void frame();
};

struct KeyEstimate { std::string name; double confidence = 0; int tonic = -1; bool minor = false; };
KeyEstimate estimateKey(const std::vector<float>& pitchFrames);

struct BpmEstimate { double bpm = 0; double confidence = 0; };
// fine: row-major fine blocks (F_NUM_COLS per row), blockSeconds: duration of one block
BpmEstimate estimateBpm(const std::vector<float>& fine, double blockSeconds);

void fft(std::vector<std::complex<double>>& a); // in place, size must be a power of two

} // namespace f8k
