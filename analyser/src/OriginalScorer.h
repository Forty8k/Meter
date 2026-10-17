// Replays CORE windows through the original Airwindows Meter 0.5.5 scoring, at text speed.
// The scoring code is the original's own (see OriginalScorer.inc), driven the way the plugin drives it in real time:
// for each audio window the editor stores the ten values, runs its INCREMENT handler, and the meter repaints once.
#pragma once
#include "Capture.h"
#include <string>
#include <memory>

namespace f8k {

struct OriginalResult {
    std::string grade;      // the full string shown at the top of the meter: bonus number, letter, then authority, power, detail
    std::string bonus, letter, authority, power, detail, directions;
    double peakPrevGrade = 0, slewPrevGrade = 0, bassPrevGrade = 0; // the running grade values behind power, detail, authority
    double outputR = 0, outputG = 0, outputB = 0;                    // slews, zero crosses, peaks scores (the tone colour)
    double cumulative = 0, duration = 0, cumulativePlus = 0, durationPlus = 0;
    double letterValue = 0;  // the number behind the letter (0-27.6, the letter is its whole part: 27 = A, higher is better)
    int bonusValue = 0;      // the bonus number before it is hidden (the original only shows 1-80; lower is better)
    long windows = 0, paints = 0;
};

class OriginalReplay {
public:
    // displayWidth 1058 x 698 is the meter area of the plugin at its default 1080 x 720 size.
    // paintEvery = 1 is real-time playback (one repaint per window). Larger values mimic a host rendering faster than the screen redraws.
    explicit OriginalReplay(int displayWidth = 1058, int displayHeight = 698, int paintEvery = 1);
    ~OriginalReplay();
    void push(const CoreWindow& w);
    OriginalResult result() const;
private:
    struct Impl; std::unique_ptr<Impl> impl;
    int paintEvery; long windows = 0, paints = 0;
};

} // namespace f8k
