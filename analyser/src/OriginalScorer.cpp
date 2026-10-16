#include "OriginalScorer.h"
#include "OriginalStub.h"
#include <array>
#include <iostream>
#include "OriginalScorer.inc"

namespace f8k {

struct OriginalReplay::Impl { f8korig::AirwindowsMeter meter; f8korig::juce::Graphics g; };

OriginalReplay::OriginalReplay(int w, int h, int pe) : impl(new Impl), paintEvery(pe < 1 ? 1 : pe)
{
    auto& meter = impl->meter;
    meter.w = w; meter.h = h;
    meter.displayWidth = w; meter.displayHeight = h; //as PluginEditor::resized() sets them
    meter.pluginColour = f8korig::juce::Colours::lightgrey;
    meter.resetArrays(); //as the PluginEditor constructor does
    meter.outputR = 10.0;
    meter.outputG = 10.0;
    meter.outputB = 10.0;
}
OriginalReplay::~OriginalReplay() = default;

void OriginalReplay::push(const CoreWindow& x)
{
    auto& meter = impl->meter; //the editor's message handling, in the order the plugin sends the messages
    meter.pushPeakL(x.peakL); meter.pushPeakR(x.peakR);
    meter.pushSlewL(x.slewL); meter.pushSlewR(x.slewR);
    meter.pushZeroL(x.zeroL); meter.pushZeroR(x.zeroR);
    meter.pushBeamL(x.beamL); meter.pushBeamR(x.beamR);
    meter.pushDeptL(x.deptL); meter.pushDeptR(x.deptR);
    f8korig::editorOnIncrement(meter);
    ++windows;
    if (windows % paintEvery == 0) {meter.paint(impl->g); ++paints;}
}

OriginalResult OriginalReplay::result() const
{
    const auto& m = impl->meter;
    OriginalResult r;
    r.bonus = m.bonusScore.s; r.letter = m.totalPackage.s; r.authority = m.authority.s; r.power = m.power.s; r.detail = m.detail.s;
    r.directions = m.directions.s;
    r.grade = r.bonus + r.letter + r.authority + r.power + r.detail; //the order the original draws it
    r.peakPrevGrade = m.peakPrevGrade; r.slewPrevGrade = m.slewPrevGrade; r.bassPrevGrade = m.bassPrevGrade;
    r.outputR = m.outputR; r.outputG = m.outputG; r.outputB = m.outputB;
    r.cumulative = m.cumulative; r.duration = m.duration; r.cumulativePlus = m.cumulativePlus; r.durationPlus = m.durationPlus;
    r.windows = windows; r.paints = paints;
    return r;
}

} // namespace f8k
