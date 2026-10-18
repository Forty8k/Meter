// meterDARK Analyser: the LAB's simulation of meterDARK's three hints (light/heavy, headroom/squashed, dark/bright).
// It is the plugin's own hint maths (Meter F8K, the build "balance line fixed in the centre"), run over the original
// Meter 0.5.5 window data stored in the library, with every constant that shapes the readings turned into a setting.
// With every setting at its default it reads exactly as that Meter F8K build does.
#pragma once
#include <vector>
#include <string>
#include <array>
#include <functional>

namespace f8k {

struct HintParam {
    const char* key;      // file/settings name
    const char* group;    // "weight", "dynamics", "detail", "all"
    const char* label;
    const char* unit;
    double lo, hi, def;
    int decimals;
    const char* help;
};
const std::vector<HintParam>& hintParams();
int hintParamIndex(const std::string& key);

struct HintSettings {
    std::vector<double> v;           // one per hintParams(), in order
    HintSettings();                  // the defaults
    double operator[](const char* key) const;
};

struct HintTrackResult {
    double weight = 0, dynamics = 0, darkRead = 0, brightRead = 0;  // average readings over the audible windows (0-1)
    double light = 0, heavy = 0, headroom = 0, squashed = 0, dark = 0, bright = 0; // share of the audible time each hint shows (0-1)
    int windows = 0, audible = 0;
};

// names/values: one track's stored Meter 0.5.5 core windows (PEAK_LEFT ... DEPT_RIGHT, as read by Library::readCore)
HintTrackResult simulateHints(const std::vector<std::string>& names, const std::vector<float>& values, const HintSettings& s);

// LEVEL TEST: how much do the readings move when the same music is played quieter? Each track's stored windows are scaled by the given
// gains (peaks, which are square roots of amplitude, by sqrt(g); slews, beam, departure and RMS by g; zero-cross lengths unchanged: exactly
// what the plugin's level modes do) and run through the hint maths again. A level-independent hint does not move.
std::vector<float> scaleWindows(const std::vector<std::string>& names, const std::vector<float>& values, double db);
struct LevelTestRow {
    double db = 0;
    double reading[4] = {};      // mean shift of weight, dynamics, dark and bright readings from the 0 dB result (signed)
    double readingAbs[4] = {};   // mean size of the shift
    double on[6] = {};           // mean shift of the share of time each hint shows (light, heavy, headroom, squashed, dark, bright), signed
    double onAbs[6] = {};        // mean size of that shift
    double audible = 0;          // mean share of the windows counted as audible
    int tracks = 0;
};
struct LevelTestTrack { const std::vector<std::string>* names; const std::vector<float>* values; };
std::vector<LevelTestRow> levelTest(const std::vector<LevelTestTrack>& tracks, const HintSettings& s, const std::vector<double>& dbs, const std::function<bool()>& shouldStop = {});
std::string levelTestReport(const std::vector<LevelTestRow>& rows);

} // namespace f8k
