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
    //tune[10] = heavy line and tune[9] = light line: where on the body meter the heavy / light shares are measured (the labels show the Hz)
    //tune[8] = hint speed: how fast all the hint averages rise and fall (rate = 0.01 + 0.07*value)
    //tune[7] = detail fill gate: a hot slew bin only fills at full strength if it was also hit in the window before; lone hits count for less (0 = the original)
    //tune[6] = detail bin decay: hot slew bins empty faster, so brief overslews fade quickly and sustained ones stay full (0 = the original decay)
    float tune[11] = {0.20f, 0.50f, 0.20f, 0.70f, 0.80f, 0.30f, 0.35f, 0.80f, 0.20f, 0.54f, 0.31f};
    static constexpr float tuneDefault[11] = {0.20f, 0.50f, 0.20f, 0.70f, 0.80f, 0.30f, 0.35f, 0.80f, 0.20f, 0.54f, 0.31f};
    float tuneUser[11] = {0.20f, 0.50f, 0.20f, 0.70f, 0.80f, 0.30f, 0.35f, 0.80f, 0.20f, 0.54f, 0.31f}; //the saved default preset (double-click on a label goes back to this)
    std::function<void()> onSaveDefault; //set by the editor: writes tune and trims to the preset file
    juce::uint32 savedAtMs = 0;
    bool tuneOpen = false;
    int tuneDragging = -1;
    float tuneDragStart = 0.0f;
    float hintDeepAvg = 0.0f;     //low end: share of recent bass dot size below the heavy line
    float bassAccTotal = 0.0f, bassAccLow = 0.0f, bassAccDeep = 0.0f; //slow averages of the bass dot size in total / below the light line / below the heavy line
    float slewHotFill = 0.0f;    //detail: how full the hottest slew bins are right now, 0-1 (shown next to the decay setting)
    float gateEffectAvg = 0.0f;  //detail: share of slew fill the gate is currently holding back (shown next to the gate setting)
    float gateKept = 0.0f, gateAll = 0.0f; //this window's slew fill after / before the gate
    float peakCeil = 0.0f;       //dynamics: slow envelope of the loudest peak position, the track's own ceiling (a limited master piles up just under it, whatever level that is)
    float hintCeilAvg = 0.0f;    //dynamics: share of recent windows sitting within a hair of that ceiling
    bool slewHitNow[totalBins+2] = {}, slewHitPrev[totalBins+2] = {}; //which slew bins were hit this window / last window (for the fill gate)
    //LIVE HINT DETECTORS: slow averages (only over audible windows) of how often the bass and the peaks reach their target zones,
    //plus the slew colour shares and the track's own peak ceiling. Index 0 = low end, 1 = dynamics.
    float hintUpperAvg[3] = {0.5f, 0.5f, 0.5f};
    float hintRedAvg = 0.0f;   //slews: share of recent dots that are red (slews well ahead of peaks)
    float hintRedRun = 0.0f, hintGreenRun = 0.0f; //how many windows in a row have been red / green: sustained runs count fully, brief ones count less
    float hintGreenAvg = 0.0f; //slews: share of recent dots that are green/teal (peaks well ahead of slews)
    int hintState[3] = {0, 0, 0}; //-1 = under target ("more"), 0 = fine, +1 = over target ("reduce")
    static constexpr float stripTopUnits = 50.0f; //height of the text strip above each meter, in the 600-high layout:
    static constexpr float stripMidUnits = 30.0f; //zero crosses (includes the top color bar), then peaks and slews
    
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
    struct ColumnMetrics {float peakL, peakR, slewL, slewR, bassL, bassR, meterZeroL, meterZeroR, psDotSizeL, psDotSizeR, slewDotSizeL, slewDotSizeR, bassDotSizeL, bassDotSizeR;};
    ColumnMetrics columnMetrics(unsigned long col) const;
    void scoreColumn(unsigned long col); //scores one finished audio window, once, as it arrives
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
        hintDeepAvg = 0.0f;
        bassAccTotal = 0.0f; bassAccLow = 0.0f; bassAccDeep = 0.0f;
        peakCeil = 0.0f;
        slewHotFill = 0.0f; gateEffectAvg = 0.0f; gateKept = 0.0f; gateAll = 0.0f;
        hintCeilAvg = 0.0f;
        for (unsigned long b = 0; b < totalBins+2; ++b) {slewHitNow[b] = false; slewHitPrev[b] = false;}
        hintRedRun = 0.0f;
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
