#pragma once
#include <cmath>
//SHARED TUNE SETTINGS TABLE. Slots 0-11 are the original hint controls (0-1 values, see AirwindowsUI.h). Slots 12-24 are the "more" controls that shape how the
//gauges read the dot clouds. They are stored as 0-1 like the others and shown in real units through the table below.
constexpr int kNumTune = 25;
constexpr int kFirstMore = 12;
constexpr float kTuneDefaults[kNumTune] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.70f, 0.30f, 0.70f, 0.10f, 0.50f, 0.30f, 0.50f,
                                           0.500f, 0.442f, 0.120f, 0.328f, 0.356f, 0.1667f, 0.500f, 0.343f, 0.4286f, 0.400f, 0.484f, 0.600f, 0.400f};
struct TuneSpec {const char* label; const char* unit; float lo; float hi; bool logScale; int decimals; int section;};
constexpr TuneSpec kTuneSpecs[kNumTune - kFirstMore] = {
    {"low push from",  " Hz", 40.0f,  160.0f,  true,  0, 0}, //12 dots below this frequency start to count extra
    {"low push full",  " Hz", 25.0f,  120.0f,  true,  0, 0}, //13 ... and count fully extra at this frequency
    {"low push amt",   "%",   0.0f,   150.0f,  false, 0, 0}, //14 how much extra (heavier) at full
    {"high push from", " Hz", 100.0f, 600.0f,  true,  0, 0}, //15 dots above this frequency start to count less
    {"high push full", " Hz", 200.0f, 1500.0f, true,  0, 0}, //16 ... and count fully less at this frequency
    {"high push amt",  "%",   0.0f,   90.0f,   false, 0, 0}, //17 how much less (lighter) at full
    {"peak influence", "x",   0.0f,   2.0f,    false, 2, 0}, //18 how much the peak part of a dot counts toward weight (0 = frequency weight only)
    {"weight range",   "",    1.0f,   8.0f,    false, 1, 0}, //19 how much dot mass moves the gauge from end to end (bigger = wider sweet spot)
    {"dyn range",      " dB", 2.0f,   16.0f,   false, 1, 1}, //20 how many dB of peak swing counts as full headroom
    {"quiet gate",     " dB", 6.0f,   36.0f,   false, 0, 1}, //21 windows more than this far under the recent ceiling are ignored (all three gauges)
    {"level follow",   " s",  0.5f,   10.0f,   true,  1, 1}, //22 how fast the mean level reference catches up
    {"run length",     " win",1.0f,   32.0f,   true,  0, 2}, //23 windows in a row before a red/green hit counts fully
    {"lone weight",    "%",   0.0f,   100.0f,  false, 0, 2}, //24 how much a single lone red/green window counts
};
inline float tuneReal(const float* t, int i) { //real-unit value of a "more" slot
    const TuneSpec& s = kTuneSpecs[i - kFirstMore];
    const float v = t[i] < 0.0f ? 0.0f : (t[i] > 1.0f ? 1.0f : t[i]);
    return s.logScale ? s.lo * powf(s.hi / s.lo, v) : s.lo + (s.hi - s.lo) * v;
}
