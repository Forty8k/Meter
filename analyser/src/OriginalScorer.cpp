#include "OriginalScorer.h"
#include "OriginalStub.h"
#include <array>
#include <iostream>
#include <cmath>
#include <map>
#include <algorithm>

namespace mdark {
// What the directions line said most of the time during the loud parts of the track (louder than half of the
// level the loudest 10% of the track reaches), with its share, e.g. "allow detail (62%)".
std::string mainDirection(const std::vector<std::string>& dirs, const std::vector<double>& vols)
{
    if (dirs.empty()) return {};
    std::vector<double> v(vols); std::sort(v.begin(), v.end());
    const double loud = 0.5*v[(size_t)((v.size() - 1)*0.9)];
    std::map<std::string, int> count; int total = 0;
    for (size_t i = 0; i < dirs.size(); ++i) if (vols[i] >= loud && !dirs[i].empty()) {++count[dirs[i]]; ++total;}
    if (total == 0) return {};
    auto best = std::max_element(count.begin(), count.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return best->first + " (" + std::to_string((int)std::lround(100.0*best->second/total)) + "%)";
}
}

namespace mdark { ScoreParams& scoreParams() { thread_local ScoreParams p; return p; } }
#include "gen/Versions.inc"

namespace mdark {
const MeterVersion* findMeterVersion(const std::string& id)
{
    for (auto& v : meterVersions()) if (v.id == id) return &v;
    return nullptr;
}
}
