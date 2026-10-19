#include "LabHints.h"
#include <cmath>
#include <algorithm>

namespace mdark {

// Defaults are the values in meterDARK (plus its factory tune settings), so with every setting at its default hintLAB reads exactly as the plugin does.
const std::vector<HintParam>& hintParams()
{
    static const std::vector<HintParam> p = {
        //---- where each hint switches on (the plugin's tune settings)
        {"tune9",  "weight",   "balance point",     "",    0.02, 0.98, 0.50, 2, "the weight reading that counts as balanced"},
        {"tune0",  "weight",   "thin distance",     "",    0.02, 0.98, 0.30, 2, "how far below the balance point counts as thin"},
        {"tune1",  "weight",   "full distance",     "",    0.02, 0.98, 0.30, 2, "how far above the balance point counts as full"},
        {"tune11", "dynamics", "balance point",     "",    0.02, 0.98, 0.50, 2, "the dynamics reading that counts as balanced"},
        {"tune2",  "dynamics", "headroom distance", "",    0.02, 0.98, 0.30, 2, "how far below the balance point counts as headroom"},
        {"tune3",  "dynamics", "squashed distance", "",    0.02, 0.98, 0.30, 2, "how far above the balance point counts as squashed"},
        {"tune18", "detail",   "balance point",     "",    0.02, 0.98, 0.50, 2, "the dark <-> bright reading that counts as balanced"},
        {"tune4",  "detail",   "dark distance",     "",    0.02, 0.98, 0.30, 2, "how far below the balance point counts as dark"},
        {"tune5",  "detail",   "bright distance",   "",    0.02, 0.98, 0.30, 2, "how far above the balance point counts as bright"},
        {"tune6",  "detail",   "detail decay",      "",    0.00, 0.98, 0.00, 2, "the bright reading falls faster once brightness stops, so brief bursts fade quickly (0 = stock)"},
        {"tune7",  "detail",   "fill gate",         "",    0.00, 0.98, 0.00, 2, "a lone bright window counts for less, more so the higher it sits (0 = stock)"},
        {"tune8",  "all",      "hint speed",        "",    0.00, 0.98, 0.10, 2, "how fast every reading follows the music"},
        //---- what shapes the readings (constants in the plugin's code)
        {"weightThinHz", "weight", "thin at",            " Hz", 100,  1000, 300,  0, "the average bass position that reads 0 (thin): music that sits around this frequency"},
        {"weightFullHz", "weight", "full at",            " Hz", 25,   150,  60,   0, "the average bass position that reads 1 (full): music that sits around this frequency"},
        {"refOffsetDb",  "weight", "reference above mean", " dB", 0,  12,   4.5,  1, "where Detail's level reference sits above the mean peak"},
        {"refTarget",    "weight", "reference target",   "",    100,  200,  170,  0, "the level the reference is brought to (170 = about -1.5 dB)"},
        {"fMax",         "weight", "level boost limit",  "x",   1,    40,   16,   1, "the most a quiet track is boosted to read like a loud one in Detail (4 = +24 dB, 16 = +48 dB)"},
        {"dynRangeDb",   "dynamics","dynamics range",    " dB", 2,    16,   8,    1, "this much peak swing reads as full headroom"},
        {"followRate",   "dynamics","level follow",      "",    0.002,0.1,  0.02, 3, "how fast the mean level reference catches up (per window)"},
        {"gateDb",       "all",    "quiet gate",         " dB", 6,    36,   18,   0, "windows this far under the recent ceiling are ignored by weight and dynamics"},
        {"ceilRise",     "all",    "ceiling rise",       "",    0.005,0.2,  0.04, 3, "how fast the ceiling rises to a louder part (per window)"},
        {"ceilFall",     "all",    "ceiling fall",       "",    0.001,0.1,  0.01, 3, "how fast it falls back in a quieter part (per window)"},
        {"detailGate",   "detail", "detail quiet gate",  "",    20,   150,  70,   0, "windows with peaks under this position (70 = about -18 dB under a full-scale ceiling) are ignored by detail"},
        {"relativeGates","all",    "gates follow level",  "",    0,    1,    1,    0, "1 = Detail's quiet gate and the quiet fade of the strips and hints follow the track's own ceiling (stems read like masters); 0 = fixed levels, as before"},
        {"detailLevelComp","detail","detail level match", "",    0,    1,    1,    0, "1 = Detail's red/green dot test is made as if the track sat at the reference level; 0 = as before"},
        {"runLength",    "detail", "run length",         " win",1,    32,   8,    0, "windows in a row before a red/green window counts fully"},
        {"lone",         "detail", "lone weight",        "",    0,    1,    0.4,  2, "how much a single red/green window counts"},
        {"tiltGain",     "detail", "tilt gain",          "",    1,    12,   4,    1, "how far the dark <-> bright gauge swings for a given bright (red) share minus dark (green) share"},
        {"keepWeight",   "all",    "hint hold",          "",    0,    0.2,  0.04, 2, "how far back past its trigger a hint holds on (stops flicker)"},
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
    const float tune6 = (float)s["tune6"], tune7 = (float)s["tune7"];
    const float tune8 = (float)s["tune8"], tune9 = (float)s["tune9"], tune11 = (float)s["tune11"], tune18 = (float)s["tune18"];
    const float thinPos = posFromHz(s["weightThinHz"]), fullPos = std::fmax(posFromHz(s["weightFullHz"]), thinPos + 1.0f);
    const float refOffset = (float)s["refOffsetDb"], refTarget = (float)s["refTarget"], fMax = (float)s["fMax"];
    const float dynRange = (float)s["dynRangeDb"], followRate = (float)s["followRate"], gateRatio = (float)std::pow(10.0, -s["gateDb"]/40.0);
    const float ceilRise = (float)s["ceilRise"], ceilFall = (float)s["ceilFall"], detailGate = (float)s["detailGate"], runLen = (float)s["runLength"], lone = (float)s["lone"];
    const float tiltGain = (float)s["tiltGain"], keep = (float)s["keepWeight"];
    const bool relGates = s["relativeGates"] > 0.5, detailComp = s["detailLevelComp"] > 0.5;
    auto ramp = [](float v, float from, float full) { //0 at "from", 1 at "full", smoothstep between (only for the retired weight reading below)
        const float w = std::fmax(std::fabs(full - from), 0.5f), dir = (full >= from) ? 1.0f : -1.0f;
        const float x = std::fmin(std::fmax(dir*(v - from)/w, 0.0f), 1.0f);
        return x*x*(3.0f - 2.0f*x);
    };
    //state, as the plugin starts it
    float weightNum = 119.0f, weightDen = 1.0f, massCeil = 0.0f, spreadMean = 0.0f, spreadAvg = 4.0f, detailF = 1.0f; bool spreadInit = false;
    float reading[3] = {0.5f, 0.5f, 0.5f}, redAvg = 0, greenAvg = 0, widthAvg = 0, redRun = 0, greenRun = 0; bool redPrev = false;
    float oldMassAvg = 3.7f, oldReading = 0.5f; //the retired weight reading, for the comparison only
    int state[3] = {0, 0, 0};
    double sumW = 0, sumD = 0, sumTilt = 0, sumWidth = 0, sumOldW = 0, cnt = 0, shows[6] = {};
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
        //WEIGHT (thin <-> full) and DYNAMICS (the plugin's maths, constants from the settings)
        const float topPeakM = std::fmax(peakL, peakR);
        if (topPeakM > 8.0f) {
            if (topPeakM > massCeil) massCeil += (topPeakM - massCeil)*ceilRise; else massCeil -= (massCeil - topPeakM)*ceilFall;
            if (topPeakM > gateRatio*massCeil) {
                const float refLevel = spreadInit ? 200.0f*std::pow(10.0f, (spreadMean + refOffset)/40.0f) : massCeil;
                const float refFloor = refTarget/fMax;
                detailF = std::fmin(std::fmax(refTarget/std::fmax(refLevel, refFloor), 1.0f), fMax);
                const float winLevel = std::fmin(topPeakM/std::fmax(massCeil, 1.0f), 1.0f), winWeight = winLevel*winLevel;
                weightNum += (winWeight*0.5f*(bassL + bassR) - weightNum)*avgRate;
                weightDen += (winWeight - weightDen)*avgRate;
                reading[0] = std::fmin(std::fmax((weightNum/std::fmax(weightDen, 1.0e-6f) - thinPos)/(fullPos - thinPos), 0.0f), 1.0f);
                {   //the retired reading (dot mass with the frequency push, the plugin's factory constants before version 11)
                    const float f = std::fmin(std::fmax(170.0f/std::fmax(refLevel, 170.0f/16.0f), 0.8f), 16.0f);
                    auto dotMass = [&](float meterZero, float peak, float dataP, float bassPos) {
                        const float push = 1.0f + 0.30f*(0.6f*ramp(bassPos, 139.0f, 160.0f) - 0.5f*ramp(bassPos, 107.0f, 82.0f));
                        return (std::sqrt(std::fmax(meterZero, 0.0f)*0.1f*dataP*f) + std::pow(std::fmin(peak*f, 200.0f)/140.0f, 4.0f))*push;
                    };
                    const float massNow = 0.5f*(dotMass(meterZeroL, peakL, dataPL, bassL) + dotMass(meterZeroR, peakR, dataPR, bassR));
                    oldMassAvg += (massNow - oldMassAvg)*avgRate;
                    oldReading = std::fmin(std::fmax((oldMassAvg - 2.0f)/3.4f, 0.0f), 1.0f);
                }
                const float dbNow = 40.0f*std::log10(std::fmax(topPeakM, 1.0f)/200.0f);
                if (!spreadInit) {spreadMean = dbNow; spreadInit = true;}
                else {spreadAvg += (std::fabs(dbNow - spreadMean) - spreadAvg)*avgRate; spreadMean += (dbNow - spreadMean)*followRate;}
                reading[1] = 1.0f - std::fmin(spreadAvg/dynRange, 1.0f);
            }
        }
        //DETAIL (the dot colours)
        const float fd = detailComp ? detailF : 1.0f;
        const float peakLd = peakL*fd, peakRd = peakR*fd, slewLd = slewL*fd, slewRd = slewR*fd;
        const float psLd = fd == 1.0f ? psL : (dataPL*fd*64.0f)/(std::fabs(peakLd*0.945433426957143f - slewLd) + 6.18033988749894f);
        const float psRd = fd == 1.0f ? psR : (dataPR*fd*64.0f)/(std::fabs(peakRd*0.945433426957143f - slewRd) + 6.18033988749894f);
        const float redNow = ((psLd <= 1.0f && slewLd > peakLd) ? 0.5f : 0.0f) + ((psRd <= 1.0f && slewRd > peakRd) ? 0.5f : 0.0f);
        const float greenNow = ((psLd <= 1.0f && slewLd <= peakLd) ? 0.5f : 0.0f) + ((psRd <= 1.0f && slewRd <= peakRd) ? 0.5f : 0.0f);
        const float gateScale = (relGates && massCeil > 0.0f) ? std::fmin(massCeil/200.0f, 1.0f) : 1.0f;
        if (std::fmax(peakL, peakR) > detailGate*gateScale) {
            redRun = (redNow > 0.5f) ? redRun + 1.0f : redRun*0.5f;
            greenRun = (greenNow > 0.5f) ? greenRun + 1.0f : greenRun*0.5f;
            //meterDARK's Detail settings, as the plugin: fill gate (a lone bright window counts less, more so the higher it sits), detail decay (faster fall)
            const float redUp = std::fmin(std::fmax((redNow > 0.0f ? std::fmax(slewLd > peakLd ? slewLd : 0.0f, slewRd > peakRd ? slewRd : 0.0f) : 0.0f)/200.0f, 0.0f), 1.0f);
            const float gateMult = (redNow > 0.0f && !redPrev) ? 1.0f - 0.6f*tune7*redUp : 1.0f;
            redPrev = redNow > 0.0f;
            const float redW = redNow*(lone + (1.0f - lone)*std::fmin(redRun/runLen, 1.0f))*gateMult;
            const float greenW = greenNow*(lone + (1.0f - lone)*std::fmin(greenRun/runLen, 1.0f));
            redAvg += (redW - redAvg)*avgRate*(redW < redAvg ? 1.0f + 4.0f*tune6 : 1.0f);
            greenAvg += (greenW - greenAvg)*avgRate;
            widthAvg += ((redW + greenW) - widthAvg)*avgRate;
        }
        reading[2] = std::fmin(std::fmax(0.5f + 0.5f*tiltGain*(redAvg - greenAvg), 0.0f), 1.0f); //dark <-> bright: one reading
        //the hints (the plugin's state machine): each gauge has a balance point and a distance either side
        for (int h = 0; h < 3; ++h) {
            const float bal = h == 0 ? tune9 : (h == 1 ? tune11 : tune18);
            const float lo = bal - (h == 0 ? tune0 : (h == 1 ? tune2 : tune4)), hi = bal + (h == 0 ? tune1 : (h == 1 ? tune3 : tune5));
            const bool overNow = reading[h] > hi, overKeep = reading[h] > hi - keep, underNow = reading[h] < lo, underKeep = reading[h] < lo + keep;
            int st = state[h];
            if (st == 1 && !overKeep) st = 0;
            if (st == -1 && !underKeep) st = 0;
            if (overNow) st = 1; else if (st == 0 && underNow) st = -1;
            state[h] = st;
        }
        //count the audible time only (the plugin fades its hints out in quiet passages)
        const float loud = std::fmax(std::fmax(dataPL, dataPR), std::fmax(v[iSL], v[iSR])); //Chris's outputVol squared
        const float quiet = (relGates && massCeil > 0.0f) ? std::fmin(loud*8.0f*0.94f/std::fmax(std::fmin(massCeil/200.0f, 0.94f), 0.05f), 1.0f) : std::fmin(loud*8.0f, 1.0f);
        if (quiet >= 0.5f) {
            cnt += 1; sumW += reading[0]; sumD += reading[1]; sumTilt += reading[2]; sumWidth += std::fmin(0.5f*tiltGain*widthAvg, 1.0f); sumOldW += oldReading;
            for (int h = 0; h < 3; ++h) {if (state[h] == -1) shows[h*2] += 1; if (state[h] == 1) shows[h*2+1] += 1;}
        }
    }
    out.windows = (int)windows; out.audible = (int)cnt;
    if (cnt > 0) {
        out.weight = sumW/cnt; out.dynamics = sumD/cnt; out.tilt = sumTilt/cnt; out.width = sumWidth/cnt; out.oldWeight = sumOldW/cnt;
        out.thin = shows[0]/cnt; out.full = shows[1]/cnt; out.headroom = shows[2]/cnt; out.squashed = shows[3]/cnt; out.dark = shows[4]/cnt; out.bright = shows[5]/cnt;
    }
    return out;
}

std::vector<float> scaleWindows(const std::vector<std::string>& names, const std::vector<float>& values, double db)
{
    const double g = std::pow(10.0, db/20.0), sg = std::sqrt(g);
    std::vector<double> mul(names.size(), 1.0);
    for (size_t i = 0; i < names.size(); ++i) {
        const auto& n = names[i];
        if (n.rfind("PEAK", 0) == 0) mul[i] = sg;
        else if (n.rfind("SLEW", 0) == 0 || n.rfind("BSLW", 0) == 0 || n.rfind("DSLW", 0) == 0 || n.rfind("BEAM", 0) == 0 || n.rfind("DEPT", 0) == 0
                 || n.rfind("BRMS", 0) == 0 || n.rfind("DRMS", 0) == 0) mul[i] = g;
    }
    std::vector<float> out(values.size());
    const size_t k = names.size();
    for (size_t i = 0; i < values.size(); ++i) out[i] = (float)(values[i]*mul[i % k]);
    return out;
}

// Scales stored windows by a gain per window (the level modes: normalised = the same gain everywhere, mix = a gain that floats).
std::vector<float> scaleWindowsEach(const std::vector<std::string>& names, const std::vector<float>& values, const std::vector<double>& gains)
{
    const size_t k = names.size();
    std::vector<int> kind(k, 0); // 0 unchanged, 1 sqrt(g), 2 g
    for (size_t i = 0; i < k; ++i) {
        const auto& n = names[i];
        if (n.rfind("PEAK", 0) == 0) kind[i] = 1;
        else if (n.rfind("SLEW", 0) == 0 || n.rfind("BSLW", 0) == 0 || n.rfind("DSLW", 0) == 0 || n.rfind("BEAM", 0) == 0 || n.rfind("DEPT", 0) == 0
                 || n.rfind("BRMS", 0) == 0 || n.rfind("DRMS", 0) == 0) kind[i] = 2;
    }
    std::vector<float> out(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const size_t w = i/k; const double g = w < gains.size() ? gains[w] : 1.0;
        out[i] = (float)(kind[i % k] == 1 ? values[i]*std::sqrt(g) : kind[i % k] == 2 ? values[i]*g : values[i]);
    }
    return out;
}

// The mix gain, worked out from the stored windows the way the plugin works it out from the audio: a running peak ceiling (a louder peak sets it at once,
// held 8 s, then falling with a 20 s time constant), gain = -1 dBFS / ceiling, limited to 0.1 ... +24 dB. One gain per window.
std::vector<double> mixWindowGains(const std::vector<std::string>& names, const std::vector<float>& values, double sampleRate)
{
    const size_t k = names.size(); if (k == 0) return {};
    const size_t n = values.size()/k;
    std::vector<int> pk;
    for (size_t i = 0; i < k; ++i) if (names[i].rfind("PEAK", 0) == 0) pk.push_back((int)i);
    const double win = 1881.0/44100.0*sampleRate;
    std::vector<double> g(n, 1.0);
    double ceiling = 0.0, hold = 0.0;
    for (size_t w = 0; w < n; ++w) {
        double p = 0.0;
        for (int i : pk) { const double v = values[w*k + (size_t)i]; p = std::max(p, v*v); } // PEAK is sent as sqrt of the amplitude
        if (p > ceiling) {ceiling = p; hold = 8.0*sampleRate;}
        else if (hold > 0) hold -= win;
        else ceiling *= std::exp(-win/(20.0*sampleRate));
        g[w] = std::min(15.848931924611133, std::max(0.1, 0.8912509381337456/std::max(ceiling, 1.0e-9)));
    }
    return g;
}

std::vector<LevelTestRow> levelTest(const std::vector<LevelTestTrack>& tracks, const HintSettings& s, const std::vector<double>& dbs, const std::function<bool()>& shouldStop)
{
    std::vector<LevelTestRow> rows(dbs.size());
    for (size_t d = 0; d < dbs.size(); ++d) rows[d].db = dbs[d];
    for (auto& t : tracks) {
        if (shouldStop && shouldStop()) break;
        if (t.names == nullptr || t.values == nullptr || t.values->empty()) continue;
        const HintTrackResult base = simulateHints(*t.names, *t.values, s);
        if (base.audible == 0) continue;
        for (size_t d = 0; d < dbs.size(); ++d) {
            const HintTrackResult r = dbs[d] == 0.0 ? base : simulateHints(*t.names, scaleWindows(*t.names, *t.values, dbs[d]), s);
            auto& row = rows[d];
            ++row.tracks;
            row.audible += (double)r.audible/std::max(1, r.windows);
            const double rd[3] = {r.weight - base.weight, r.dynamics - base.dynamics, r.tilt - base.tilt};
            const double on[6] = {r.thin - base.thin, r.full - base.full, r.headroom - base.headroom, r.squashed - base.squashed, r.dark - base.dark, r.bright - base.bright};
            for (int i = 0; i < 3; ++i) {row.reading[i] += rd[i]; row.readingAbs[i] += std::fabs(rd[i]);}
            for (int i = 0; i < 6; ++i) {row.on[i] += on[i]; row.onAbs[i] += std::fabs(on[i]);}
            if (r.audible == 0) { /* the track vanished at this level: its hints all read 0 */ }
        }
    }
    for (auto& row : rows) if (row.tracks > 0) {
        const double n = row.tracks;
        row.audible /= n;
        for (int i = 0; i < 3; ++i) {row.reading[i] /= n; row.readingAbs[i] /= n;}
        for (int i = 0; i < 6; ++i) {row.on[i] /= n; row.onAbs[i] /= n;}
    }
    return rows;
}

std::string levelTestReport(const std::vector<LevelTestRow>& rows)
{
    char b[400];
    std::string out;
    snprintf(b, sizeof b, "Level test: the same tracks played quieter, compared with 0 dB (%d tracks). Mean size of the shift; target = 0.\n\n", rows.empty() ? 0 : rows[0].tracks);
    out += b;
    snprintf(b, sizeof b, "%-8s %-21s %-48s %s\n", "level", "readings (0-1)", "share of time each hint shows (0-1)", "audible");
    out += b;
    snprintf(b, sizeof b, "%-8s %-6s %-6s %-6s  %-7s %-7s %-7s %-7s %-7s %-7s %s\n", "", "weight", "dynam", "tilt", "thin", "full", "headrm", "squash", "dark", "bright", "share");
    out += b;
    for (auto& r : rows) {
        snprintf(b, sizeof b, "%+5.0f dB %6.3f %6.3f %6.3f  %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f  %5.2f\n", r.db, r.readingAbs[0], r.readingAbs[1], r.readingAbs[2],
                 r.onAbs[0], r.onAbs[1], r.onAbs[2], r.onAbs[3], r.onAbs[4], r.onAbs[5], r.audible);
        out += b;
    }
    return out;
}

} // namespace mdark
