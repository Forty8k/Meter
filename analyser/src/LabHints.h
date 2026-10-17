// Meter F8K Analyser: the LAB's simulation of Meter F8K's three hints (light/heavy, headroom/squashed, dark/bright).
// It is the plugin's own hint maths (Meter F8K, the build "balance line fixed in the centre"), run over the original
// Meter 0.5.5 window data stored in the library, with every constant that shapes the readings turned into a setting.
// With every setting at its default it reads exactly as that Meter F8K build does.
#pragma once
#include <vector>
#include <string>
#include <array>

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

} // namespace f8k
