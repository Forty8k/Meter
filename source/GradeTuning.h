#pragma once
// GRADELAB: the scoring constants the Analyser's gradeLAB exports to <user app data>/meterDARK/grade-tuning.xml. meterDARK feeds them to its tuned copy of
// Chris's meter (never to Chris's own copy), only when they were tuned on the same original Meter version it is built on. Read on start and whenever the file
// changes (checked about once a second). No file, another version, or switched off by a click on "gradeLAB" = Chris's grade exactly.
#include <juce_core/juce_core.h>
#include <map>

struct GradeTuning {
    std::map<juce::String, double> values; juce::String version; juce::Time loadedTime; bool fromFile = false;
    static juce::File file() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("meterDARK").getChildFile("grade-tuning.xml"); }
    bool reloadIfChanged() { //true when something changed
        const juce::File f = file();
        if (!f.existsAsFile()) { if (fromFile) { *this = GradeTuning(); return true; } return false; }
        const juce::Time t = f.getLastModificationTime();
        if (fromFile && t == loadedTime) return false;
        std::unique_ptr<juce::XmlElement> x(juce::XmlDocument::parse(f));
        if (x == nullptr || !x->hasTagName("metergradetuning")) return false;
        GradeTuning n;
        for (int i = 0; i < x->getNumAttributes(); ++i) {
            const juce::String k = x->getAttributeName(i);
            if (k == "meterVersion") n.version = x->getAttributeValue(i);
            else if (k != "exported" && k != "ver") n.values[k] = x->getAttributeValue(i).getDoubleValue();
        }
        n.loadedTime = t; n.fromFile = true;
        *this = n;
        return true;
    }
};
