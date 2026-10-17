#include "LabHints.h"
#include <cmath>
#include <algorithm>

namespace f8k {

// Defaults are the values hard-coded in meterDARK (plus its factory tune settings). The push frequencies are the exact
// frequencies of the meter positions the plugin uses (139/160 and 107/82), so the defaults read exactly as the plugin.
const std::vector<HintParam>& hintParams()
{
    static const std::vector<HintParam> p = {
        //---- where each hint switches on (the plugin's tune settings)
        {"tune9",  "weight",   "balance point",     "",    0.02, 0.98, 0.50, 2, "the weight reading that counts as balanced"},
        {"tune0",  "weight",   "light distance",    "",    0.02, 0.98, 0.30, 2, "how far below the balance point counts as light"},
        {"tune1",  "weight",   "heavy distance",    "",    0.02, 0.98, 0.30, 2, "how far above the balance point counts as heavy"},
        {"tune11", "dynamics", "balance point",     "",    0.02, 0.98, 0.50, 2, "the dynamics reading that counts as balanced"},
        {"tune2",  "dynamics", "headroom distance", "",    0.02, 0.98, 0.30, 2, "how far below the balance point counts as headroom"},
        {"tune3",  "dynamics", "squashed distance", "",    0.02, 0.98, 0.30, 2, "how far above the balance point counts as squashed"},
        {"tune4",  "detail",   "dark level",        "",    0.02, 0.98, 0.30, 2, "dark shows when the dark reading falls below this"},
        {"tune5",  "detail",   "bright level",      "",    0.02, 0.98, 0.70, 2, "bright shows when the bright reading rises above this"},
        {"tune8",  "all",      "hint speed",        "",    0.00, 0.98, 0.10, 2, "how fast every reading follows the music"},
        {"tune10", "weight",   "frequency push",    "",    0.00, 0.98, 0.30, 2, "overall strength of the low/high push below"},
        //---- what shapes the readings (constants in the plugin's code)
        {"lowPushHz",    "weight", "low push from",      " Hz", 40,   160,  75.80539928587282, 0, "dots below this count extra (heavier)"},
        {"lowPushFullHz","weight", "low push full at",   " Hz", 25,   120,  47.36542359104531, 0, "and count fully extra at this frequency"},
        {"lowPushGain",  "weight", "low push amount",    "",    0,    2,    0.6,  2, "how much extra, times the frequency push"},
        {"highPushHz",   "weight", "high push from",     " Hz", 100,  600,  174.4399740149014, 0, "dots above this count less (lighter)"},
        {"highPushFullHz","weight","high push full at",  " Hz", 200,  1500, 381.027329265194, 0, "and count fully less at this frequency"},
        {"highPushGain", "weight", "high push amount",   "",    0,    1,    0.5,  2, "how much less, times the frequency push"},
        {"peakInfluence","weight", "peak influence",     "x",   0,    2,    1,    2, "how much the peak part of each dot counts (0 = frequency weight only)"},
        {"peakDiv",      "weight", "peak part scale",    "",    80,   220,  140,  0, "the peak part is (peak / this) to the power below"},
        {"peakPow",      "weight", "peak part power",    "",    1,    6,    4,    1, "higher = only the loudest peaks add weight"},
        {"massOffset",   "weight", "mass at 0",          "",    0,    5,    2.0,  2, "the dot mass that reads as 0 (lightest)"},
        {"massRange",    "weight", "mass range",         "",    0.5,  8,    3.4,  2, "how much mass moves the reading from 0 to 1 (bigger = wider sweet spot)"},
        {"refOffsetDb",  "weight", "reference above mean", " dB", 0,  12,   4.5,  1, "where the level reference sits above the mean peak"},
        {"refTarget",    "weight", "reference target",   "",    100,  200,  170,  0, "the level the reference is brought to (170 = about -1.5 dB)"},
        {"fMax",         "weight", "level boost limit",  "x",   1,    16,   4,    1, "the most a quiet track is boosted to read like a loud one"},
        {"dynRangeDb",   "dynamics","dynamics range",    " dB", 2,    16,   8,    1, "this much peak swing reads as full headroom"},
        {"followRate",   "dynamics","level follow",      "",    0.002,0.1,  0.02, 3, "how fast the mean level reference catches up (per window)"},
        {"gateDb",       "all",    "quiet gate",         " dB", 6,    36,   18,   0, "windows this far under the recent ceiling are ignored by weight and dynamics"},
        {"ceilRise",     "all",    "ceiling rise",       "",    0.005,0.2,  0.04, 3, "how fast the ceiling rises to a louder part (per window)"},
        {"ceilFall",     "all",    "ceiling fall",       "",    0.001,0.1,  0.01, 3, "how fast it falls back in a quieter part (per window)"},
        {"detailGate",   "detail", "detail quiet gate",  "",    20,   150,  70,   0, "windows with peaks under this position (70 = about -18 dB) are ignored by detail"},
        {"runLength",    "detail", "run length",         " win",1,    32,   8,    0, "windows in a row before a red/green window counts fully"},
        {"lone",         "detail", "lone weight",        "",    0,    1,    0.4,  2, "how much a single red/green window counts"},
        {"brightSpread", "detail", "bright spread",      "",    1,    30,   13.5, 1, "spreads the red share over the bright reading"},
        {"keepWeight",   "all",    "hint hold",          "",    0,    0.2,  0.04, 2, "how far back past its trigger a weight or dynamics hint holds on (stops flicker)"},
    };
    return p;
}

int hintParamIndex(const std::string& key)
{
    const auto& p = hintParams();
    for (size_t i = 0; i < p.size(); ++i) if (key == p[i].key) return (int)i;
    return -1;
}

HintSettings::HintSettings() { for (auto& p : hintParams()) v.push_back(p.def); }
double HintSettings::operator[](const char* key) const { const int i = hintParamIndex(key); return i >= 0 ? v[(size_t)i] : 0.0; }

static float posFromHz(double hz) { return (float)std::fmin(std::sqrt(std::fmax(std::sqrt(22050.0/std::fmax(hz, 1.0))*6.0 - 6.0, 0.0))*16.2 - 20.0, 199.0); }

HintTrackResult simulateHints(const std::vector<std::string>& names, const std::vector<float>& values, const HintSettings& s)
{
    HintTrackResult out;
    int iPL = -1, iPR = -1, iSL = -1, iSR = -1, iZL = -1, iZR = -1;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto& n = names[i];
        if (n == "PEAK_LEFT") iPL = (int)i; else if (n == "PEAK_RIGHT") iPR = (int)i; else if (n == "SLEW_LEFT") iSL = (int)i;
        else if (n == "SLEW_RIGHT") iSR = (int)i; else if (n == "ZERO_LEFT") iZL = (int)i; else if (n == "ZERO_RIGHT") iZR = (int)i;
    }
    if (iPL < 0 || iPR < 0 || iSL < 0 || iSR < 0 || iZL < 0 || iZR < 0 || names.empty()) return out;
    const size_t k = names.size();
    //settings
    const float tune0 = (float)s["tune0"], tune1 = (float)s["tune1"], tune2 = (float)s["tune2"], tune3 = (float)s["tune3"], tune4 = (float)s["tune4"], tune5 = (float)s["tune5"];
    const float tune8 = (float)s["tune8"], tune9 = (float)s["tune9"], tune10 = (float)s["tune10"], tune11 = (float)s["tune11"];
    const float lowFrom = posFromHz(s["lowPushHz"]), lowFull = posFromHz(s["lowPushFullHz"]), highFrom = posFromHz(s["highPushHz"]), highFull = posFromHz(s["highPushFullHz"]);
    const float lowGain = (float)s["lowPushGain"], highGain = (float)s["highPushGain"], peakInfl = (float)s["peakInfluence"], peakDiv = (float)s["peakDiv"], peakPow = (float)s["peakPow"];
    const float massOffset = (float)s["massOffset"], massRange = (float)s["massRange"], refOffset = (float)s["refOffsetDb"], refTarget = (float)s["refTarget"], fMax = (float)s["fMax"];
    const float dynRange = (float)s["dynRangeDb"], followRate = (float)s["followRate"], gateRatio = (float)std::pow(10.0, -s["gateDb"]/40.0);
    const float ceilRise = (float)s["ceilRise"], ceilFall = (float)s["ceilFall"], detailGate = (float)s["detailGate"], runLen = (float)s["runLength"], lone = (float)s["lone"];
    const float brightSpread = (float)s["brightSpread"], keep = (float)s["keepWeight"];
    auto ramp = [](float v, float from, float full) { //0 at "from", 1 at "full", smoothstep between
        const float w = std::fmax(std::fabs(full - from), 0.5f), dir = (full >= from) ? 1.0f : -1.0f;
        const float x = std::fmin(std::fmax(dir*(v - from)/w, 0.0f), 1.0f);
        return x*x*(3.0f - 2.0f*x);
    };
    //state, as the plugin starts it
    float massAvg = 3.7f, massCeil = 0.0f, spreadMean = 0.0f, spreadAvg = 4.0f; bool spreadInit = false;
    float reading[2] = {0.5f, 0.5f}, redAvg = 0, greenAvg = 0, redRun = 0, greenRun = 0;
    int state[3] = {0, 0, 0};
    double sumW = 0, sumD = 0, sumDark = 0, sumBright = 0, cnt = 0, shows[6] = {};
    const size_t windows = values.size()/k;
    for (size_t w = 0; w < windows; ++w) {
        const float* v = &values[w*k];
        const float dataPL = v[iPL], dataPR = v[iPR];
        //the meter's own per-window numbers (as columnMetrics)
        const float peakL = dataPL*200.0f, peakR = dataPR*200.0f;
        const float slewL = std::sqrt(v[iSL])*300.0f, slewR = std::sqrt(v[iSR])*300.0f;
        float meterZeroL = std::sqrt(v[iZL])*6.0f - 6.0f; if (meterZeroL > 192.0f) meterZeroL = 192.0f;
        float meterZeroR = std::sqrt(v[iZR])*6.0f - 6.0f; if (meterZeroR > 192.0f) meterZeroR = 192.0f;
        const float bassL = std::fmin(std::sqrt(meterZeroL)*16.2f - 20.0f, 199.0f), bassR = std::fmin(std::sqrt(meterZeroR)*16.2f - 20.0f, 199.0f);
        const float psL = (dataPL*64.0f)/(std::fabs(peakL*0.945433426957143f - slewL) + 6.18033988749894f);
        const float psR = (dataPR*64.0f)/(std::fabs(peakR*0.945433426957143f - slewR) + 6.18033988749894f);
        const float avgRate = 0.01f + 0.07f*tune8;
        //WEIGHT and DYNAMICS (the plugin's maths, constants from the settings)
        const float topPeakM = std::fmax(peakL, peakR);
        if (topPeakM > 8.0f) {
            if (topPeakM > massCeil) massCeil += (topPeakM - massCeil)*ceilRise; else massCeil -= (massCeil - topPeakM)*ceilFall;
            if (topPeakM > gateRatio*massCeil) {
                const float refLevel = spreadInit ? 200.0f*std::pow(10.0f, (spreadMean + refOffset)/40.0f) : massCeil;
                const float f = std::fmin(std::fmax(refTarget/std::fmax(refLevel, 40.0f), 0.8f), fMax);
                auto dotMass = [&](float meterZero, float peak, float dataP, float bassPos) {
                    const float push = 1.0f + tune10*(lowGain*ramp(bassPos, lowFrom, lowFull) - highGain*ramp(bassPos, highFrom, highFull));
                    return (std::sqrt(std::fmax(meterZero, 0.0f)*0.1f*dataP*f) + peakInfl*std::pow(std::fmin(peak*f, 200.0f)/peakDiv, peakPow))*push;
                };
                const float massNow = 0.5f*(dotMass(meterZeroL, peakL, dataPL, bassL) + dotMass(meterZeroR, peakR, dataPR, bassR));
                massAvg += (massNow - massAvg)*avgRate;
                reading[0] = std::fmin(std::fmax((massAvg - massOffset)/massRange, 0.0f), 1.0f);
                const float dbNow = 40.0f*std::log10(std::fmax(topPeakM, 1.0f)/200.0f);
                if (!spreadInit) {spreadMean = dbNow; spreadInit = true;}
                else {spreadAvg += (std::fabs(dbNow - spreadMean) - spreadAvg)*avgRate; spreadMean += (dbNow - spreadMean)*followRate;}
                reading[1] = 1.0f - std::fmin(spreadAvg/dynRange, 1.0f);
            }
        }
        //DETAIL (the dot colours)
        const float redNow = ((psL <= 1.0f && slewL > peakL) ? 0.5f : 0.0f) + ((psR <= 1.0f && slewR > peakR) ? 0.5f : 0.0f);
        const float greenNow = ((psL <= 1.0f && slewL <= peakL) ? 0.5f : 0.0f) + ((psR <= 1.0f && slewR <= peakR) ? 0.5f : 0.0f);
        if (std::fmax(peakL, peakR) > detailGate) {
            redRun = (redNow > 0.5f) ? redRun + 1.0f : redRun*0.5f;
            greenRun = (greenNow > 0.5f) ? greenRun + 1.0f : greenRun*0.5f;
            redAvg += (redNow*(lone + (1.0f - lone)*std::fmin(redRun/runLen, 1.0f)) - redAvg)*avgRate;
            greenAvg += (greenNow*(lone + (1.0f - lone)*std::fmin(greenRun/runLen, 1.0f)) - greenAvg)*avgRate;
        }
        //the hints (the plugin's state machine)
        const float brightLive = 1.0f - std::exp(-brightSpread*redAvg), darkRead = 1.0f - greenAvg;
        for (int h = 0; h < 3; ++h) {
            bool overNow, overKeep, underNow, underKeep;
            if (h == 2) {
                overNow = brightLive > tune5; overKeep = brightLive > std::fmax(tune5 - 0.06f, 0.0f);
                underNow = darkRead < tune4; underKeep = darkRead < std::fmin(tune4 + 0.10f, 1.0f);
            } else {
                const float bal = h == 0 ? tune9 : tune11, lo = bal - (h == 0 ? tune0 : tune2), hi = bal + (h == 0 ? tune1 : tune3);
                overNow = reading[h] > hi; overKeep = reading[h] > hi - keep; underNow = reading[h] < lo; underKeep = reading[h] < lo + keep;
            }
            int st = state[h];
            if (st == 1 && !overKeep) st = 0;
            if (st == -1 && !underKeep) st = 0;
            if (overNow) st = 1; else if (st == 0 && underNow) st = -1;
            state[h] = st;
        }
        //count the audible time only (the plugin fades its hints out in quiet passages)
        const float outputVol = std::sqrt(std::fmax(std::fmax(dataPL, dataPR), std::fmax(v[iSL], v[iSR])));
        if (std::fmin(outputVol*outputVol*8.0f, 1.0f) >= 0.5f) {
            cnt += 1; sumW += reading[0]; sumD += reading[1]; sumDark += darkRead; sumBright += brightLive;
            for (int h = 0; h < 3; ++h) {if (state[h] == -1) shows[h*2] += 1; if (state[h] == 1) shows[h*2+1] += 1;}
        }
    }
    out.windows = (int)windows; out.audible = (int)cnt;
    if (cnt > 0) {
        out.weight = sumW/cnt; out.dynamics = sumD/cnt; out.darkRead = sumDark/cnt; out.brightRead = sumBright/cnt;
        out.light = shows[0]/cnt; out.heavy = shows[1]/cnt; out.headroom = shows[2]/cnt; out.squashed = shows[3]/cnt; out.dark = shows[4]/cnt; out.bright = shows[5]/cnt;
    }
    return out;
}

} // namespace f8k
