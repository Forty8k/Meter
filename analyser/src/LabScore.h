// The Lab's settings for the original Meter's scoring (see ScoreParams in OriginalScorer.h): labels, ranges, help.
#pragma once
#include "OriginalScorer.h"
#include <vector>

namespace mdark {

struct ScoreParamInfo { const char* key; const char* label; double lo, hi; int decimals; const char* help; double ScoreParams::* member; };

inline const std::vector<ScoreParamInfo>& scoreParamInfo()
{
    static const std::vector<ScoreParamInfo> v = {
        {"decayPeak",  "power bin decay",     0.02, 0.6, 3, "how fast the peak meter's bins empty on each redraw (lower = slower, longer memory)", &ScoreParams::decayPeak},
        {"decaySlew",  "detail bin decay",    0.02, 0.6, 3, "the same for the slew meter", &ScoreParams::decaySlew},
        {"decayBass",  "authority bin decay", 0.02, 0.6, 3, "the same for the zero cross meter", &ScoreParams::decayBass},
        {"binCap",     "bin limit",           0.5,  4.0, 3, "how full a bin can get: how much one busy level can count", &ScoreParams::binCap},
        {"curvePS",    "power/detail weighting", 0.5, 3.15, 3, "the weighting across the peak and slew meters (1.571 = favours the top, 3.142 = favours the middle)", &ScoreParams::curvePS},
        {"curveBass",  "authority weighting", 0.5, 3.15, 3, "the weighting across the zero cross meter (3.142 = favours the middle)", &ScoreParams::curveBass},
        {"gradeExp",   "grade curve",         1.0, 2.5, 3, "scores are raised to this power before they become grades (higher = spreads the top grades out)", &ScoreParams::gradeExp},
        {"gradeRise",  "grade rise speed",    0.01, 0.5, 3, "how fast a grade climbs to a higher score (grades never fall)", &ScoreParams::gradeRise},
        {"balanceExp", "balance curve",       1.0, 3.0, 3, "how the balance of the three scores is curved before it adds up", &ScoreParams::balanceExp},
        {"letterExp",  "letter curve",        1.0, 3.0, 3, "how overall balance turns into the letter", &ScoreParams::letterExp},
        {"clipLevel",  "clip level",          180, 200, 1, "peak position that counts as clipping (200 = 0 dB)", &ScoreParams::clipLevel},
        {"clipDecay",  "clip allowance (one channel)", 0.9, 1.0, 3, "how fast sustained clipping on one channel shortens the allowance", &ScoreParams::clipDecay},
        {"clipDecayBoth", "clip allowance (both)", 0.9, 1.0, 3, "how much faster when both channels clip", &ScoreParams::clipDecayBoth},
        {"clipWeight", "clip length weight",  0.0, 4.0, 3, "how much clip length moves the peak bins", &ScoreParams::clipWeight},
        {"blackLine",  "black line weight",   0.0, 1.5, 3, "the black lines' weight in the bonus number", &ScoreParams::blackLine},
        {"toneDecay",  "tone colour pull",    0.0, 0.5, 3, "how much the three scores are pulled towards each other each window", &ScoreParams::toneDecay},
        {"detailDecay", "meterDARK detail decay", 0.0, 0.98, 2, "meterDARK's setting: hot slew bins empty faster, so brief bright bursts count less (0 = the original). As meterDARK with \"grade\" ticked.", &ScoreParams::detailDecay},
        {"fillGate",   "meterDARK fill gate",  0.0, 0.98, 2, "meterDARK's setting: a hot slew bin fills fully only if it was hit in the window before (0 = the original). As meterDARK with \"grade\" ticked.", &ScoreParams::fillGate},
        {"width",      "meter width",         200, 5150, 0, "how many windows the meter holds on screen (1058 = the default plugin size). The original's score depends on it.", &ScoreParams::width},
    };
    return v;
}

} // namespace mdark
