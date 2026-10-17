// The ORIGINAL Airwindows Meter, every version we have the source of, run at text speed.
// For each version the analyser runs that version's own audio loop (PluginProcessor::processBlock) on the track,
// keeps the messages it sends to its display for every window (the "core" data), and drives that version's own
// editor message handling and meter scoring with them exactly as the plugin does in real time: one repaint per window.
// The code for each version is generated from its untouched source by tools/make_original_scorer.py.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace f8k {

struct OriginalResult {
    std::string grade;      // the full string shown at the top of the meter: bonus number, letter, then authority, power, detail
    std::string bonus, letter, authority, power, detail;
    std::string directions;     // what the directions line said at the end of the track
    std::string directionsMain; // what it said most of the time during the loud parts, e.g. "allow detail (62%)"
    double peakPrevGrade = 0, slewPrevGrade = 0, bassPrevGrade = 0; // the running values behind power, detail, authority
    double outputR = 0, outputG = 0, outputB = 0;                    // slews, zero crosses, peaks scores (the tone colour)
    double cumulative = 0, duration = 0, cumulativePlus = 0, durationPlus = 0;
    double letterValue = 0;  // the number behind the letter (0-27.6, the letter is its whole part: 27 = A, higher is better)
    int bonusValue = 0;      // the bonus number before it is hidden (the original only shows 1-80; lower is better)
    long windows = 0, paints = 0;
};

class MeterRun {
public:
    virtual ~MeterRun() = default;
    virtual void process(const float* L, const float* R, int numSamples) = 0; // live audio, consecutive chunks
    virtual bool feedWindow(const std::vector<std::string>& names, const float* values) = 0; // replay one stored window
    virtual OriginalResult result() const = 0;
    std::vector<std::string> names; // the messages of one window, in the order the plugin sends them (INCREMENT not included)
    std::vector<float> core;        // every window's values, names.size() per window
    size_t windowCount() const { return names.empty() ? 0 : core.size() / names.size(); }
};

struct MeterVersion {
    std::string id;      // folder name, e.g. "0.5.6-40bb6cc"
    std::string version; // Chris's version number, e.g. "0.5.6"
    std::string commit, date;
    // displayWidth 1058 x 698 is the meter area of the plugin at its default 1080 x 720 size.
    // paintEvery = 1 is real-time playback (one repaint per window); larger mimics a host rendering faster than the screen redraws.
    std::function<std::unique_ptr<MeterRun>(double sampleRate, int paintEvery)> create;
};
const std::vector<MeterVersion>& meterVersions(); // oldest first

// THE LAB'S EXPERIMENTAL SCORING: a copy of the newest version whose constants come from these settings (per thread).
// With every setting at its default it scores exactly as the original.
struct ScoreParams {
    double decayPeak = 0.1618033988749894, decaySlew = 0.1618033988749894, decayBass = 0.1618033988749894; // how fast each meter's bins empty, per paint
    double binCap = 1.618033988749894;      // how full a bin can get
    double curvePS = 1.57079632679489661923, curveBass = 3.14159265358979323846; // the weighting curve across each meter (sin up to this)
    double gradeExp = 1.618033988749894;    // scores are raised to this power before they become grades
    double gradeRise = 0.1;                 // how fast a grade climbs towards a higher score (it never falls)
    double balanceExp = 1.618033988749894;  // the curve applied to the balance of the three scores
    double letterExp = 1.618033988749894;   // the curve from overall balance to the letter
    double clipLevel = 197.0;               // peak position that counts as clipping (200 = 0 dB)
    double clipDecay = 0.99, clipDecayBoth = 0.975; // how fast sustained clipping shortens the allowance (one channel, both)
    double clipWeight = 1.618033988749894;  // how much clip length moves the peak bins
    double blackLine = 0.3;                 // the black line's weight in the bonus number
    double toneDecay = 0.125;               // how much the scores decay towards each other with the tone colour each window
    double width = 1058;                    // the meter's width in pixels = how many windows it holds on screen
};
ScoreParams& scoreParams();                                   // this thread's settings
const std::string& tunableVersionId();
const std::vector<std::string>& tunableSettingsUsed();        // the settings this version's code actually has
std::unique_ptr<MeterRun> createTunableRun(double sampleRate, int paintEvery);
const MeterVersion* findMeterVersion(const std::string& id);

} // namespace f8k
