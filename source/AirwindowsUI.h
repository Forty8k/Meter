// AirwindowsUI by Chris Johnson
// Initial seed code for the meter created by Paul Walker on 8/23/21.

#ifndef AIRWINDOWSUI_H
#define AIRWINDOWSUI_H
#include <functional>
#include "PluginProcessor.h"


class AirwindowsLookAndFeel : public juce::LookAndFeel_V4
{
public:

    AirwindowsLookAndFeel()
    {
        setColour(juce::Slider::backgroundColourId, juce::Colours::red);
        setColour(juce::Slider::thumbColourId, juce::Colours::grey);
        setColour(juce::Slider::trackColourId, juce::Colours::grey);
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::grey);
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black); //track after thumb
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::grey);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::grey);
        setColour(juce::Slider::textBoxHighlightColourId, juce::Colours::grey);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::grey);
        setColour(juce::TextButton::buttonColourId, juce::Colours::lightgrey);
        setColour(juce::TextButton::buttonOnColourId, juce::Colours::lightgrey);
        setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
        setColour(juce::TextButton::textColourOnId, juce::Colours::grey);
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colours::lightgrey);
        
        juce::String newWidth = juce::String(); juce::String namedWidth = JucePlugin_Name; namedWidth.append("Width",1024);
        juce::String newHeight = juce::String(); juce::String namedHeight = JucePlugin_Name; namedHeight.append("Height",1024);
        juce::String namedFont = JucePlugin_Name; namedFont.append("Font",1024);
        juce::String newColour = juce::String(); juce::String namedColour = JucePlugin_Name; namedColour.append("Colour",1024);
        juce::String newImage = juce::String(); juce::String namedImage = JucePlugin_Name; namedImage.append("Image",1024);
        juce::String newApplyTrackColourAmount = juce::String(); juce::String namedApplyTrackColourAmount = JucePlugin_Name; namedApplyTrackColourAmount.append("TrackColourAmount",1024);
        juce::String newLEDColour = juce::String(); juce::String namedLEDColour = JucePlugin_Name; namedLEDColour.append("LEDColour",1024);
        juce::String newKnobMode = juce::String(); juce::String namedKnobMode = JucePlugin_Name; namedKnobMode.append("KnobMode",1024);
        juce::String newInterpolation = juce::String(); juce::String namedInterpolation = JucePlugin_Name; namedInterpolation.append("Interpolation",1024);
        juce::String newTilt = juce::String(); juce::String namedTilt = JucePlugin_Name; namedInterpolation.append("Tilt",1024);

        juce::File customSettings = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userDocumentsDirectory).getChildFile("Airwindows").getChildFile("AirwindowsGlobals.txt");
        juce::String xmlFile = customSettings.loadFileAsString();
        std::unique_ptr<juce::XmlElement> body (juce::XmlDocument::parse (xmlFile));
        if ((xmlFile != juce::String()) && (xmlFile.length() > 0)) {
            for (auto* e : body->getChildIterator()) {
                if (e->hasTagName ("option")) { // find the "PARAM" sub-element
                    juce::String attributeValueAsString = e->getStringAttribute("id");
                    if (attributeValueAsString.equalsIgnoreCase("userWidth") && newWidth == juce::String()) newWidth = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedWidth)) newWidth = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userHeight") && newHeight == juce::String()) newHeight = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedHeight)) newHeight = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userFont") && newFont == juce::String()) newFont = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedFont)) newFont = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userColour") && newColour == juce::String()) newColour = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedColour)) newColour = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userImage") && newImage == juce::String()) newImage = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedImage)) {newImage = e->getStringAttribute("value"); usingNamedImage = true;}
                    if (attributeValueAsString.equalsIgnoreCase("userTrackColourAmount") && newApplyTrackColourAmount == juce::String()) newApplyTrackColourAmount = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedApplyTrackColourAmount)) newApplyTrackColourAmount = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userLEDColour") && newLEDColour == juce::String()) newLEDColour = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedLEDColour)) newLEDColour = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userKnobMode") && newKnobMode == juce::String()) newKnobMode = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedKnobMode)) newKnobMode = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userInterpolation") && newInterpolation == juce::String()) newInterpolation = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedInterpolation)) newInterpolation = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase("userTilt") && newTilt == juce::String()) newTilt = e->getStringAttribute("value");
                    if (attributeValueAsString.equalsIgnoreCase(namedTilt)) newTilt = e->getStringAttribute("value");
                }
            }
            body.release();
        }
        
        juce::File customBackground = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userDocumentsDirectory).getChildFile("Airwindows").getChildFile (newImage);
        if (customBackground.existsAsFile()) {
            backgroundImage = juce::ImageFileFormat::loadFrom(juce::File(customBackground));
            blurImage = backgroundImage.rescaled(3, 3);
         }
        userWidth = newWidth.getIntValue(); if (userWidth < 8 || userWidth > 16386) userWidth = 1080;
        userHeight = newHeight.getIntValue(); if (userHeight < 8 || userHeight > 16386) userHeight = 720;
        //if you've not specified anything or your settings are crazy enough we go with defaults
        
        bool TARGET_WIN32 = false;
        bool TARGET_MAC = false;
#ifdef JUCE_WINDOWS
        TARGET_WIN32 = true;
#endif
#ifdef JUCE_MAC
        TARGET_MAC = true;
#endif
        if (newColour.length() > 0) { //something has been specified, could be anything
            useToneColor = newColour.equalsIgnoreCase("tonecolor");
            if (newColour.equalsIgnoreCase("darkmode")) {//specify it and you get it (even if system is different)
                if (TARGET_WIN32) defaultColour = juce::Colour(0xFF1A1A1A);//XP Zune 0xFF1A1A1A
                else defaultColour = juce::Colour(0xFF1C1C1E);//macOS dark mode 0xFF1C1C1E
            } else {
                if (newColour.equalsIgnoreCase("lightmode")) {//specify it and you get it (even if system is different)
                    if (TARGET_MAC) defaultColour = juce::Colour(0xFFF2F2F7);//macOS lightmode 0xFFF2F2F7
                    else defaultColour = juce::Colours::white;
                } else { //give what's being specified if at all possible, or just grey if the color is unknown
                    defaultColour = juce::Colours::findColourForName(newColour, juce::Colours::lightgrey);
                }
            }
        } else { //fall back through to default, nothing has been specified or there's no AirwindowsGlobals.txt
            if (juce::Desktop::getInstance().isDarkModeActive()) {//whatever the system, user's running dark mode
                if (TARGET_WIN32) defaultColour = juce::Colour(0xFF1A1A1A);//XP Zune 0xFF1A1A1A
                else defaultColour = juce::Colour(0xFF1C1C1E);//macOS dark mode 0xFF1C1C1E
            } else defaultColour = juce::Colours::lightgrey; //nothing specified AND not dark mode: light grey will set off the meter better :)
        } //and we have now sorted out all the color variations
        applyTrackColour = fmax(fmin(newApplyTrackColourAmount.getFloatValue(),1.0f),0.0f);
        LEDColour = juce::Colours::findColourForName(newLEDColour, juce::Colours::red);
        applyTilt = fmax(fmin(newTilt.getFloatValue(),1.0f),0.0f) * 0.5f; //value is 0-1 but in use it's 0-0.5

        knobMode = 1; //defaults to vertical 'cos I was told to
        if (newKnobMode.equalsIgnoreCase("rotary")) knobMode = 0;
        if (newKnobMode.equalsIgnoreCase("airwindows")) knobMode = 0;
        if (newKnobMode.equalsIgnoreCase("realistic")) knobMode = 0;
        if (newKnobMode.equalsIgnoreCase("vertical")) knobMode = 1;
        if (newKnobMode.equalsIgnoreCase("up and down")) knobMode = 1;
        if (newKnobMode.equalsIgnoreCase("up-and-down")) knobMode = 1;
        if (newKnobMode.equalsIgnoreCase("normal")) knobMode = 1; //I'm told vertical is customary ;)
        if (newKnobMode.equalsIgnoreCase("default")) knobMode = 1; // so here ya go ;)
        if (newKnobMode.equalsIgnoreCase("horizontal")) knobMode = 2;
        if (newKnobMode.equalsIgnoreCase("sideways")) knobMode = 2;
        if (newKnobMode.equalsIgnoreCase("horizontalvertical")) knobMode = 3;
        if (newKnobMode.equalsIgnoreCase("verticalhorizontal")) knobMode = 3;
        if (newKnobMode.equalsIgnoreCase("horizontal-vertical")) knobMode = 3;
        if (newKnobMode.equalsIgnoreCase("vertical-horizontal")) knobMode = 3;
        if (newKnobMode.equalsIgnoreCase("diagonal")) knobMode = 3;
        if (newKnobMode.equalsIgnoreCase("both")) knobMode = 3;

        alfInterpolation = 2; //defaults to bicubic 'cloud' interpolation
        if (newInterpolation.equalsIgnoreCase("none")) alfInterpolation = 0;
        if (newInterpolation.equalsIgnoreCase("off")) alfInterpolation = 0;
        if (newInterpolation.equalsIgnoreCase("nearestneighbor")) alfInterpolation = 0;
        if (newInterpolation.equalsIgnoreCase("nearest-neighbor")) alfInterpolation = 0;
        if (newInterpolation.equalsIgnoreCase("nearest neighbor")) alfInterpolation = 0; //variations on mondrian-meter
        if (newInterpolation.equalsIgnoreCase("bilinear")) alfInterpolation = 1; //option for softer definition
     }
    int userWidth;
    int userHeight;
    juce::Colour defaultColour = juce::Colours::lightgrey;
    juce::Image backgroundImage = juce::Image();
    juce::Image blurImage = juce::Image();
    juce::String newFont = juce::String();
    bool usingNamedImage = false;
    bool useToneColor = false;
    float applyTrackColour = 0.5;
    juce::Colour LEDColour = juce::Colours::red;
    int knobMode;
    int alfInterpolation;
    float applyTilt = 0.0;
};

struct AirwindowsMeter : public juce::Component
{
    void mouseDown(const juce::MouseEvent &event) override;
    void mouseDrag(const juce::MouseEvent &event) override;
    void mouseUp(const juce::MouseEvent &event) override;
    void paint(juce::Graphics &g) override;

    static constexpr int dataPoints = 5151;
    static constexpr int totalBins = 14;
    juce::Colour backdropColour = juce::Colours::white;
    juce::Colour cachedColour = juce::Colours::white;
    juce::Colour pluginColour = juce::Colours::white; //will be set to the plugin's background color: for blanking the tonecolor bars
    int displayWidth = 1280;
    int displayHeight = 720;
    unsigned long dataPosition = 0;
    int peakGrade = 0;
    int slewGrade = 0;
    int bassGrade = 0;
    double peakPrevGrade = 0.0;
    double slewPrevGrade = 0.0;
    double bassPrevGrade = 0.0;
    double cumulative = 0.0000001;
    double duration = 0.00001;
    double cumulativePlus = 0.0000001;
    double durationPlus = 0.000001;
    juce::String totalPackage = juce::String();
    juce::String bonusScore = juce::String();
    juce::String hintTextMore[3];
    juce::String hintTextLess[3];
    juce::String hintText[3]; //per-meter hints, drawn in each meter's top strip: zero crosses, peaks, slews
    float hintOpacity[3] = {0.0f, 0.0f, 0.0f};
    juce::String power = juce::String();
    juce::String detail = juce::String();
    juce::String authority = juce::String();
    double storeR;
    double storeG;
    double storeB; //these are used ONLY to draw colors on screen
    double outputR;
    double outputG;
    double outputB; //these get values applied to them to produce scores
    double cyanFix; //need this to run inside a switch statement
    double outputMax;
    double outputMin;
    double gradientMin; //need the simpler form for GFX
    double outputVol;
    double outputShift;
    double sustainedClip;
    bool meterPlaying;
    //SLEW TRIM: a display-only zoom of the slew meter, dragged up or down on its label in the slews strip (double-click = 0).
    //Colours, dot sizes, scores and grades always use the full-level data. This only changes where the slew dots and beam are drawn.
    float slewTrimDb = 0.0f;
    float peakTrimDb = 0.0f; //the same kind of display-only trim for the peak meter (drawn positions only; clip detection and scores stay at full level)
    int trimDragging = 0; //0 = none, 1 = peak trim being dragged, 2 = slew trim being dragged
    float trimDragStart = 0.0f;
    //HINT TUNING (a small "tune" label at the bottom right shows or hides it): where each hint switches on, 0-1. Index = section*2 + (0 = the "more" hint
    //light / headroom / dark, 1 = the "less" hint heavy / squashed / bright). Saved with the project, defaults are the calibrated values.
    //WEIGHT: one reading (the mass of the weight dots, 0-1) on one gauge with three points: tune[9] = the balance point, tune[0] = how far BELOW it counts as light,
    //tune[1] = how far ABOVE it counts as heavy. tune[10] = frequency push: how much dots below ~80 Hz count extra (heavier) and dots above ~180 Hz count less (lighter).
    //tune[8] = hint speed: how fast all the hint averages rise and fall (rate = 0.01 + 0.07*value)
    //tune[6] = detail decay, tune[7] = fill gate: keep brief bright transients from swaying the Detail readings (0 = stock).
    //  decay: the bright reading falls faster once the brightness stops, so a brief burst fades quickly and a sustained one stays.
    //  gate: a bright window counts fully only if the window before was bright too; a lone one counts for less, more so the higher up the meter it sits.
    //tune[12] = grade (0 or 1): when ticked, the same two settings also go into the scoring (a copy of Chris's meter with them added to his slew bins),
    //  so the Detail tone strip, the tone colour and the grade change too. Unticked, or with both at 0, the grade is exactly Chris's.
    //DYNAMICS: the same idea. The reading is how flat the peaks are (0 = peaks swing a lot, 1 = every window peaks at the same level), measured as how far each window's
    //peak lands from the slow mean peak level, so it does not depend on how loud the material is. tune[11] = the balance point, tune[2] = how far BELOW it counts as headroom
    //(undercompressed), tune[3] = how far ABOVE it counts as squashed (a flat line, a test tone, an over-compressed or saturated sound).
    float tune[kTuneCount] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.70f, 0.00f, 0.00f, 0.10f, 0.50f, 0.30f, 0.50f, 0.00f, 0.00f, 0.00f, kTuneInputAtDefault, kTuneAsIfAtDefault};
    static constexpr float tuneDefault[kTuneCount] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.70f, 0.00f, 0.00f, 0.10f, 0.50f, 0.30f, 0.50f, 0.00f, 0.00f, 0.00f, kTuneInputAtDefault, kTuneAsIfAtDefault};
    float tuneUser[kTuneCount] = {0.30f, 0.30f, 0.30f, 0.30f, 0.30f, 0.70f, 0.00f, 0.00f, 0.10f, 0.50f, 0.30f, 0.50f, 0.00f, 0.00f, 0.00f, kTuneInputAtDefault, kTuneAsIfAtDefault}; //the saved default preset (double-click on a label goes back to this)
    //LEVEL (not hints, they are about what Chris's scoring is given; see PluginProcessor::feedChris). tune[13] = anchor rate (0 off, .5 = 44.1 kHz, 1 = 96 kHz): the
    //audio is converted to that rate before Chris's loop. tune[14] = level mode (0 original, .5 normalised, 1 mix). tune[15] = "input at" and tune[16] = "score as if at"
    //(LUFS = -30 + 24 x value) for normalised. Everything on screen stays at the real level; only the scoring and the grade see the mode's gain (grade letter N or M).
    int levelMode = 0; //set by the editor from tune[14]: 0 original, 1 normalised, 2 mix
    std::function<void()> onSaveDefault; //set by the editor: writes tune and trims to the preset file
    juce::uint32 savedAtMs = 0;
    //The bright reading is 1 - exp(-brightSpread * red share). It is spread so that the bright threshold sits 30% in from the far right edge, like squashed (about .70).
    static constexpr float brightSpread = 13.5f; //(the Lab setting lab.brightSpread is what the hints use; this is its factory value)
    //LAB TUNING: the constants that shape the hint readings. Factory values below; the Analyser's Lab can export new ones to
    //<user app data>/meterDARK/lab-tuning.xml, which is read on start and again whenever it changes (checked about once a second).
    struct LabTuning {
        float lowFromPos = 139.0f, lowFullPos = 160.0f, highFromPos = 107.0f, highFullPos = 82.0f; //push ramps, in meter positions
        float lowPushGain = 0.6f, highPushGain = 0.5f, peakInfluence = 1.0f, peakDiv = 140.0f, peakPow = 4.0f;
        float massOffset = 2.0f, massRange = 3.4f, refOffsetDb = 4.5f, refTarget = 170.0f, fMax = 16.0f; //fMax: the most a quiet track is boosted to read like a loud one (4 = +24 dB before, 16 = +48 dB)
        float dynRangeDb = 8.0f, followRate = 0.02f, gateDb = 18.0f, ceilRise = 0.04f, ceilFall = 0.01f;
        float detailGate = 70.0f, runLength = 8.0f, lone = 0.4f, brightSpread = 13.5f, keepWeight = 0.04f;
        //LEVEL-INDEPENDENT HINTS (1 = on, 0 = how it was before): relativeGates = Detail's quiet gate and the quiet fade of the tone strips and hints follow the track's own
        //ceiling instead of fixed levels, so a quiet stem is read like a loud master. detailLevelComp = Detail's red/green dot test is made as if the track sat at the reference level.
        float relativeGates = 1.0f, detailLevelComp = 1.0f;
        float fMin = 0.8f; //weight: the most a LOUD track is turned down to read like the reference (0.8 = about -2 dB; 0.6 makes hot masters read the same as quiet copies of themselves)
        juce::Time loadedTime; bool fromFile = false;
        float gateRatio() const { return gateDb == 18.0f ? 0.355f : powf(10.0f, -gateDb/40.0f); } //18 dB = the factory 0.355 exactly
        static float posFromHz(double hz) { return (float)fmin(sqrt(fmax(sqrt(22050.0/fmax(hz, 1.0))*6.0 - 6.0, 0.0))*16.2 - 20.0, 199.0); }
        static float ramp(float v, float from, float full) { //0 at "from", 1 at "full", smoothstep between
            const float w = fmax(fabsf(full - from), 0.5f), dir = (full >= from) ? 1.0f : -1.0f;
            const float x = fmin(fmax(dir*(v - from)/w, 0.0f), 1.0f);
            return x*x*(3.0f - 2.0f*x);
        }
        static juce::File file() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("meterDARK").getChildFile("lab-tuning.xml"); }
        void reloadIfChanged() {
            const juce::File f = file();
            if (!f.existsAsFile()) {if (fromFile) {*this = LabTuning();} return;}
            const juce::Time t = f.getLastModificationTime();
            if (fromFile && t == loadedTime) return;
            std::unique_ptr<juce::XmlElement> x(juce::XmlDocument::parse(f));
            if (x == nullptr || !x->hasTagName("meterlabtuning")) return;
            LabTuning n;
            auto get = [&](const char* k, float& v) {if (x->hasAttribute(k)) v = (float)x->getDoubleAttribute(k, v);};
            double hz;
            if (x->hasAttribute("lowPushHz")) {hz = x->getDoubleAttribute("lowPushHz"); n.lowFromPos = posFromHz(hz);}
            if (x->hasAttribute("lowPushFullHz")) {hz = x->getDoubleAttribute("lowPushFullHz"); n.lowFullPos = posFromHz(hz);}
            if (x->hasAttribute("highPushHz")) {hz = x->getDoubleAttribute("highPushHz"); n.highFromPos = posFromHz(hz);}
            if (x->hasAttribute("highPushFullHz")) {hz = x->getDoubleAttribute("highPushFullHz"); n.highFullPos = posFromHz(hz);}
            get("lowPushGain", n.lowPushGain); get("highPushGain", n.highPushGain); get("peakInfluence", n.peakInfluence); get("peakDiv", n.peakDiv); get("peakPow", n.peakPow);
            get("massOffset", n.massOffset); get("massRange", n.massRange); get("refOffsetDb", n.refOffsetDb); get("refTarget", n.refTarget); get("fMax", n.fMax);
            get("dynRangeDb", n.dynRangeDb); get("followRate", n.followRate); get("gateDb", n.gateDb); get("ceilRise", n.ceilRise); get("ceilFall", n.ceilFall);
            get("fMin", n.fMin); get("relativeGates", n.relativeGates); get("detailLevelComp", n.detailLevelComp); get("detailGate", n.detailGate); get("runLength", n.runLength); get("lone", n.lone); get("brightSpread", n.brightSpread); get("keepWeight", n.keepWeight);
            n.massRange = fmax(n.massRange, 0.05f); n.dynRangeDb = fmax(n.dynRangeDb, 0.1f); n.runLength = fmax(n.runLength, 1.0f); n.peakDiv = fmax(n.peakDiv, 1.0f);
            n.loadedTime = t; n.fromFile = true;
            *this = n;
        }
    } lab;
    int labCheckCounter = 24;
    bool tuneOpen = false;
    int tuneDragging = -1;
    float tuneDragStart = 0.0f;
    float massAvg = 3.7f;        //weight: slow average of the weight dot size (peak part measured against the track's own ceiling), see scoreColumn
    float quietNow = 1.0f;       //the quiet fade (0-1) of the latest window: tone strips and hint opacity use it (fixed level, or relative to the track's ceiling: see relativeQuiet)
    float detailF = 1.0f;        //detail: how much the track is brought up to the reference level for its dot test (1 = not at all), set by the weight block
    float relativeQuiet(float p) const; //p = the window's loudness (the larger of the peak and slew readings, as Chris's outputVol squared)
    float massCeil = 0.0f;       //weight and dynamics: the track's recent peak ceiling (rises in about a second, falls back in about 4), used only for their quiet gate
    float slewHotFill = 0.0f;    //detail: how full the hottest slew bins are right now, 0-1 (shown next to the decay setting)
    float gateEffectAvg = 0.0f;  //detail: share of slew fill the gate is currently holding back (shown next to the gate setting)
    float gateKept = 0.0f, gateAll = 0.0f; //this window's slew fill after / before the gate
    float spreadMean = 0.0f, spreadAvg = 4.0f; //dynamics: slow mean of the peak level in dB, and the slow average of how far each window's peak lands from it (the spread)
    bool spreadInit = false;
    bool slewHitNow[totalBins+2] = {}, slewHitPrev[totalBins+2] = {}; //which slew bins were hit this window / last window (for the fill gate)
    //LIVE HINT DETECTORS: slow averages (only over audible windows) of how often the bass and the peaks reach their target zones,
    //plus the slew colour shares and the track's own peak ceiling. Index 0 = low end, 1 = dynamics.
    float hintUpperAvg[3] = {0.5f, 0.5f, 0.5f};
    float hintRedAvg = 0.0f;   //slews: share of recent dots that are red (slews well ahead of peaks)
    bool gradeTuned = false; //set by the editor: the grade and tone colours come from the tuned copy of Chris's meter (shown as * after the grade)
    bool hintRedPrev = false; //the window before was red (for the fill gate on the bright reading)
    float hintRedRun = 0.0f, hintGreenRun = 0.0f; //how many windows in a row have been red / green: sustained runs count fully, brief ones count less
    float hintGreenAvg = 0.0f; //slews: share of recent dots that are green/teal (peaks well ahead of slews)
    int hintState[3] = {0, 0, 0}; //-1 = under target ("more"), 0 = fine, +1 = over target ("reduce")
    static constexpr float railUnits = 16.0f;      //the dark control rail along the bottom of each text strip: just the labels (hint numbers, display trims, tune settings). The gauges themselves are drawn on the tone strip above it.
    static constexpr float stripTopUnits = 50.0f + railUnits; //height of the text strip above each meter, in the 600-high layout:
    static constexpr float stripMidUnits = 30.0f + railUnits; //zero crosses (includes the top color bar), then peaks and slews
    
    std::array<float, totalBins+2> peakTrack; //arrays can be counted from zero and have X elements
    std::array<float, totalBins+2> slewTrack; //there's a place where Meter works with <= totalBins
    std::array<float, totalBins+2> bassTrack; //let's make sure the arrays don't exceed their boundaries
    
    std::array<float, dataPoints+2> dataPL;
    std::array<float, dataPoints+2> dataPR;
    std::array<float, dataPoints+2> dataSL;
    std::array<float, dataPoints+2> dataSR;
    std::array<float, dataPoints+2> dataZL;
    std::array<float, dataPoints+2> dataZR;
    std::array<float, dataPoints+2> dataBL;
    std::array<float, dataPoints+2> dataBR;
    std::array<float, dataPoints+2> dataDL;
    std::array<float, dataPoints+2> dataDR;
    std::array<float, dataPoints+2> backR;//backdrop red channel: loud
    std::array<float, dataPoints+2> backG;//backdrop green channel: bright
    std::array<float, dataPoints+2> backB;//backdrop blue channel: bass
    std::array<float, dataPoints+2> devZ; //per-meter tone history, drawn as shading behind each meter's text strip:
    std::array<float, dataPoints+2> devP; //how far that meter sits above (+) or below (-) the average of the other two
    std::array<float, dataPoints+2> devS; //zero crosses, peaks, slews. 0 = in balance

    void pushPeakL(float X) {dataPL[dataPosition] = X; dataPL[dataPosition+1] = X;}
    void pushPeakR(float X) {dataPR[dataPosition] = X; dataPR[dataPosition+1] = X;}
    void pushSlewL(float X) {dataSL[dataPosition] = X; dataSL[dataPosition+1] = X;}
    void pushSlewR(float X) {dataSR[dataPosition] = X; dataSR[dataPosition+1] = X;}
    void pushZeroL(float X) {dataZL[dataPosition] = X; dataZL[dataPosition+1] = X;}
    void pushZeroR(float X) {dataZR[dataPosition] = X; dataZR[dataPosition+1] = X;}
    void pushBeamL(float X) {dataBL[dataPosition] = X; dataBL[dataPosition+1] = X;}
    void pushBeamR(float X) {dataBR[dataPosition] = X; dataBR[dataPosition+1] = X;}
    void pushDeptL(float X) {dataDL[dataPosition] = X; dataDL[dataPosition+1] = X;}
    void pushDeptR(float X) {dataDR[dataPosition] = X; dataDR[dataPosition+1] = X;}
    //newer lines from Chris's meter (0.5.6 on): where the zero cross and RMS settle (B) and how far they keep departing (D)
    std::array<float, dataPoints+2> dataBZL{}, dataBZR{}, dataDZL{}, dataDZR{}, dataBRL{}, dataBRR{}, dataDRL{}, dataDRR{};
    bool hasZeroRmsLines = false;
    void pushExtra(const juce::String& what, float X) {
        std::array<float, dataPoints+2>* a = nullptr;
        if (what == "BZER_LEFT") a = &dataBZL; else if (what == "BZER_RIGHT") a = &dataBZR; else if (what == "DZER_LEFT") a = &dataDZL; else if (what == "DZER_RIGHT") a = &dataDZR;
        else if (what == "BRMS_LEFT") a = &dataBRL; else if (what == "BRMS_RIGHT") a = &dataBRR; else if (what == "DRMS_LEFT") a = &dataDRL; else if (what == "DRMS_RIGHT") a = &dataDRR;
        if (a == nullptr) return;
        (*a)[dataPosition] = X; (*a)[dataPosition+1] = X;
        hasZeroRmsLines = true;
    }
    struct ColumnMetrics {float peakL, peakR, slewL, slewR, bassL, bassR, meterZeroL, meterZeroR, psDotSizeL, psDotSizeR, slewDotSizeL, slewDotSizeR, bassDotSizeL, bassDotSizeR;};
    ColumnMetrics columnMetrics(unsigned long col) const;
    void scoreColumn(unsigned long col); //reads one finished audio window, once, as it arrives (hints and per-meter tone; the grades are Chris's)
    std::function<void()> onReset; //set by the editor: a double-click also restarts Chris's meter underneath
    double scoreSustainedClip = 180.0;
    void pushIncrement() {
        scoreColumn(dataPosition); //the window that just finished is scored here, not while painting
        dataPosition++;
        if (dataPosition >= (unsigned long)fmin(displayWidth,5150)) dataPosition = 0;
    }

    void resetArrays(){
        dataPosition = 0;
        peakGrade = 0;
        slewGrade = 0;
        bassGrade = 0;
        peakPrevGrade = 0.0;
        slewPrevGrade = 0.0;
        bassPrevGrade = 0.0; //to bump a grade you have to exceed it persistently
        totalPackage = juce::String();
        bonusScore = juce::String();
        for (int hint = 0; hint < 3; ++hint) {
            hintText[hint] = juce::String();
            hintOpacity[hint] = 0.0f;
            hintUpperAvg[hint] = 0.5f;
            hintState[hint] = 0;
        }
        hintRedAvg = 0.0f;
        hintGreenAvg = 0.0f;
        massAvg = 3.7f; massCeil = 0.0f; detailF = 1.0f; quietNow = 1.0f;
        spreadMean = 0.0f; spreadAvg = 4.0f; spreadInit = false;
        slewHotFill = 0.0f; gateEffectAvg = 0.0f; gateKept = 0.0f; gateAll = 0.0f;
        for (unsigned long b = 0; b < totalBins+2; ++b) {slewHitNow[b] = false; slewHitPrev[b] = false;}
        hintRedRun = 0.0f; hintRedPrev = false;
        hintGreenRun = 0.0f;
        cumulative = 0.0000001;
        duration = 0.00001;
        cumulativePlus = 0.0000001;
        durationPlus = 0.00001;
        power = juce::String();
        detail = juce::String();
        authority = juce::String();
        storeR = 1.0f;
        storeG = 1.0f;
        storeB = 1.0f;
        cyanFix = 1.0f;
        outputR = 10.0f;
        outputG = 10.0f;
        outputB = 10.0f;
        outputMin = 0.0f;
        gradientMin = 0.0f;
        outputVol = 0.0f;
        sustainedClip = 180.0f;
        scoreSustainedClip = 180.0;
        meterPlaying = true;
        backdropColour = juce::Colours::white;
        cachedColour = juce::Colours::white;
        
        for (unsigned long count = 0; count < totalBins; ++count) //count through all the points in the array
        {
            peakTrack[count] = 0.0f;
            slewTrack[count] = 0.0f;
            bassTrack[count] = 0.0f;
        }
        for (unsigned long count = 0; count < dataPoints; ++count) //count through all the points in the array
        {
            dataPL[count] = 0.0f;
            dataPR[count] = 0.0f;
            dataSL[count] = 0.0f;
            dataSR[count] = 0.0f;
            dataZL[count] = 0.0f;
            dataZR[count] = 0.0f;
            dataBL[count] = 0.0f;
            dataBR[count] = 0.0f;
            dataDL[count] = 0.0f;
            dataDR[count] = 0.0f;
            devZ[count] = 0.0f;
            devP[count] = 0.0f;
            devS[count] = 0.0f;
            dataBZL[count] = 0.0f; dataBZR[count] = 0.0f; dataDZL[count] = 0.0f; dataDZR[count] = 0.0f; //Chris's 0.5.6 lines
            dataBRL[count] = 0.0f; dataBRR[count] = 0.0f; dataDRL[count] = 0.0f; dataDRR[count] = 0.0f;
            backR[count] = pluginColour.getFloatRed();
            backG[count] = pluginColour.getFloatGreen();
            backB[count] = pluginColour.getFloatBlue();
        }
        repaint(); //and update the UI to immediately see the result
    }
};

extern AirwindowsLookAndFeel airwindowsLookAndFeel;
extern AirwindowsMeter meter;

#endif // AIRWINDOWSUI_H
