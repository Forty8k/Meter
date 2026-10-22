#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
: AudioProcessorEditor (&p), processorRef (p)
{
    juce::ignoreUnused (processorRef);
    setResizable(true, true);
    setLookAndFeel(&airwindowsLookAndFeel);
    if (hostTrackColour != juce::Colour()) {
        airwindowsLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, hostTrackColour);
        airwindowsLookAndFeel.setColour(juce::Slider::thumbColourId, hostTrackColour);
    } else {
        airwindowsLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, airwindowsLookAndFeel.defaultColour);
        airwindowsLookAndFeel.setColour(juce::Slider::thumbColourId, airwindowsLookAndFeel.defaultColour);
    }
    updateTrackProperties();
    updatePluginSize();
    
    idleTimer = std::make_unique<IdleTimer>(this);
    idleTimer->startTimer(1000/30); //space between UI screen updates. Larger is slower updates to screen
    
    meter.pluginColour = airwindowsLookAndFeel.defaultColour;
    meter.peakTrimDb = lastPeakTrimSeen = processorRef.peakTrimDb;
    meter.slewTrimDb = lastSlewTrimSeen = processorRef.slewTrimDb; //pick up the slew trim saved with the project
    for (int i = 0; i < kTuneCount; ++i) {meter.tune[i] = lastTuneSeen[i] = processorRef.hintTune[i]; meter.tuneUser[i] = processorRef.userTune[i];}
    meter.onSaveDefault = [this]() {
        processorRef.saveUserDefaults(meter.tune, meter.peakTrimDb, meter.slewTrimDb);
        for (int i = 0; i < kTuneCount; ++i) meter.tuneUser[i] = processorRef.userTune[i];
    };
    meter.setOpaque(true);
    meter.resetArrays();
    meter.addMouseListener(this, false);
    meter.chrisVersion = kChrisVersion; //which of Chris's looks meterDARK's own drawing follows (0.5.7: very bright detail turns the slew line yellow)
    addAndMakeVisible(meter);
    meter.outputR = 10.0;
    meter.outputG = 10.0;
    meter.outputB = 10.0;
    resetChris();
    meter.onReset = [this] { resetChris(); };
    setSize (airwindowsLookAndFeel.userWidth, airwindowsLookAndFeel.userHeight);
    // Make sure that before the constructor has finished, you've set the editor's size to whatever you need it to be.
}

PluginEditor::~PluginEditor(){
    setLookAndFeel(nullptr); //we do need to set the look and feel to null pointer
}

void PluginEditor::paint (juce::Graphics& g)
{
    if (airwindowsLookAndFeel.alfInterpolation == 0) g.setImageResamplingQuality(g.lowResamplingQuality);
    if (airwindowsLookAndFeel.alfInterpolation == 1) g.setImageResamplingQuality(g.mediumResamplingQuality);
    if (airwindowsLookAndFeel.alfInterpolation == 2) g.setImageResamplingQuality(g.highResamplingQuality); //copy this section to other stretched-bitmap meters
    if (airwindowsLookAndFeel.blurImage == juce::Image()) { // == means image is BLANK so we do a color
        g.fillAll (airwindowsLookAndFeel.defaultColour);
        if (hostTrackColour != juce::Colour()) {
            g.setFillType(juce::FillType(hostTrackColour)); g.setOpacity(airwindowsLookAndFeel.applyTrackColour);
        }
        airwindowsLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, airwindowsLookAndFeel.defaultColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
        airwindowsLookAndFeel.setColour(juce::Slider::thumbColourId, airwindowsLookAndFeel.defaultColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
        airwindowsLookAndFeel.setColour(juce::TextButton::buttonColourId, airwindowsLookAndFeel.defaultColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
        airwindowsLookAndFeel.setColour(juce::TextButton::buttonOnColourId, airwindowsLookAndFeel.defaultColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
        meter.pluginColour = airwindowsLookAndFeel.defaultColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour);
        if (airwindowsLookAndFeel.useToneColor) {
            g.fillAll (meter.backdropColour);
            airwindowsLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, meter.backdropColour);
            airwindowsLookAndFeel.setColour(juce::Slider::thumbColourId, meter.backdropColour);
            airwindowsLookAndFeel.setColour(juce::TextButton::buttonColourId, meter.backdropColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
            airwindowsLookAndFeel.setColour(juce::TextButton::buttonOnColourId, meter.backdropColour.interpolatedWith (hostTrackColour, airwindowsLookAndFeel.applyTrackColour));
        } else {
            g.fillAll (airwindowsLookAndFeel.defaultColour);
        }
    } else { //we have an image that might be named and actual size, or might be the overall tiled texture you can use
        if (airwindowsLookAndFeel.usingNamedImage) {
            g.drawImageWithin(airwindowsLookAndFeel.backgroundImage, 0, 0, getLocalBounds().getWidth(), getLocalBounds().getHeight(), 0);
        } else {
            g.setTiledImageFill(airwindowsLookAndFeel.backgroundImage, 0, 0, 1.0f); g.fillAll();
        }
        
        if (hostTrackColour != juce::Colour()) {
            g.setFillType(juce::FillType(hostTrackColour)); g.setOpacity(airwindowsLookAndFeel.applyTrackColour); g.fillAll();
        }
        airwindowsLookAndFeel.defaultColour = juce::Colour::fromRGBA(airwindowsLookAndFeel.blurImage.getPixelAt(1,1).getRed(),airwindowsLookAndFeel.blurImage.getPixelAt(1,1).getGreen(),airwindowsLookAndFeel.blurImage.getPixelAt(1,1).getBlue(),1.0);
        airwindowsLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, airwindowsLookAndFeel.defaultColour);
        airwindowsLookAndFeel.setColour(juce::Slider::thumbColourId, airwindowsLookAndFeel.defaultColour);
        airwindowsLookAndFeel.setColour(juce::TextButton::buttonColourId, airwindowsLookAndFeel.defaultColour);
        airwindowsLookAndFeel.setColour(juce::TextButton::buttonOnColourId, airwindowsLookAndFeel.defaultColour);
        meter.pluginColour = airwindowsLookAndFeel.defaultColour;
    } //find the color of the background tile or image, if there is one. Please use low-contrast stuff, but I'm not your mom :)
    
    
    if (airwindowsLookAndFeel.newFont == juce::String()) airwindowsLookAndFeel.newFont = "Jost";
    float whiteLevel = (findColour(juce::ResizableWindow::backgroundColourId).getLightness()+0.37f)*0.618f; //apply about half of this
    float blackLevel = 1.0f-whiteLevel;
    whiteLevel *= whiteLevel;
    blackLevel *= blackLevel;
    
    auto area = getLocalBounds(); // this is a huge huge routine, but not all of it runs at all times!
    auto linewidth = (int)fmin(area.getHeight(),area.getWidth());
    linewidth = (int)cbrt(linewidth/2)-1;
    area.reduce((int)(linewidth*1.618f), (int)(linewidth*1.618f));
    g.setColour(juce::Colours::white); g.setOpacity(whiteLevel);
    g.fillRect(0, 0, getLocalBounds().getWidth()-linewidth, linewidth);
    g.fillRect(0, linewidth, linewidth, getLocalBounds().getHeight()-(linewidth*2));
    g.setColour(juce::Colours::black); g.setOpacity(blackLevel);
    g.fillRect(linewidth, getLocalBounds().getHeight()-linewidth, getLocalBounds().getWidth(), linewidth);
    g.fillRect(getLocalBounds().getWidth()-linewidth, linewidth, linewidth, getLocalBounds().getHeight()-linewidth);
    
    g.setColour (juce::Colours::black);
    g.setOpacity(0.384f); g.drawRect(2, 2, getLocalBounds().getWidth()-4, getLocalBounds().getHeight()-4);
    g.setOpacity(0.618f); g.drawRect(1, 1, getLocalBounds().getWidth()-2, getLocalBounds().getHeight()-2);
    g.setOpacity(1.000f); g.drawRect(0, 0, getLocalBounds().getWidth(), getLocalBounds().getHeight());
    //draw global bevel effect, either from the color or from the color of the blurred texture, and a black border
}

void PluginEditor::resized()
{
    auto area = getLocalBounds();
    processorRef.pluginWidth = airwindowsLookAndFeel.userWidth = area.getWidth();
    processorRef.pluginHeight = airwindowsLookAndFeel.userHeight = area.getHeight();
    auto linewidth = area.getWidth();
    if (area.getHeight() > linewidth) linewidth = area.getHeight();
    linewidth = (int)cbrt(linewidth/2)-1;
    area.reduce((int)(linewidth*1.618f), (int)(linewidth*1.618f));
    meter.displayWidth = (int)area.getWidth();
    meter.displayHeight = (int)area.getHeight(); //meter-only display
    meter.setBounds(area);
    //Chris's meter underneath is the same size as yours, so it scores the same number of windows on screen as his plugin would
    chrisMeter.w = chrisMeter.displayWidth = meter.displayWidth;
    chrisMeter.h = chrisMeter.displayHeight = meter.displayHeight;
    chrisTunedMeter.w = chrisTunedMeter.displayWidth = meter.displayWidth;
    chrisTunedMeter.h = chrisTunedMeter.displayHeight = meter.displayHeight;
}

void PluginEditor::sliderValueChanged(juce::Slider *s) {}                           //there are no sliders
void PluginEditor::sliderDragStarted(juce::Slider *s) {sliderDragInternal(s, true);}      //on this plugin
void PluginEditor::sliderDragEnded(juce::Slider *s) {sliderDragInternal(s, false);} //so this section does
void PluginEditor::sliderDragInternal(juce::Slider *s, bool bv) {if (bv) sliderValueChanged(s);} //nothing

void PluginEditor::updateTrackProperties() {hostTrackColour=processorRef.trackProperties.colour; hostTrackName=processorRef.trackProperties.name; repaint();}


void PluginEditor::updatePluginSize() {
    airwindowsLookAndFeel.userWidth = processorRef.pluginWidth;
    airwindowsLookAndFeel.userHeight = processorRef.pluginHeight;
    if (airwindowsLookAndFeel.userWidth < 8 || airwindowsLookAndFeel.userWidth > 16386) airwindowsLookAndFeel.userWidth = 1280;
    if (airwindowsLookAndFeel.userHeight < 8 || airwindowsLookAndFeel.userHeight > 16386) airwindowsLookAndFeel.userHeight = 720;
    repaint();
}

//Chris's meter, as his plugin starts it
void PluginEditor::resetChris()
{
    chrisMeter.w = chrisMeter.displayWidth = meter.displayWidth;
    chrisMeter.h = chrisMeter.displayHeight = meter.displayHeight;
    chrisMeter.pluginColour = airwindowsLookAndFeel.defaultColour;
    chrisMeter.resetArrays();
    chrisMeter.outputR = 10.0; chrisMeter.outputG = 10.0; chrisMeter.outputB = 10.0;
    chrisTunedMeter.w = chrisTunedMeter.displayWidth = meter.displayWidth;
    chrisTunedMeter.h = chrisTunedMeter.displayHeight = meter.displayHeight;
    chrisTunedMeter.pluginColour = airwindowsLookAndFeel.defaultColour;
    chrisTunedMeter.resetArrays();
    for (auto& b : chrisTunedMeter.mdHitPrev) b = false;
    for (auto& b : chrisTunedMeter.mdHitNow) b = false;
    chrisTunedMeter.outputR = 10.0; chrisTunedMeter.outputG = 10.0; chrisTunedMeter.outputB = 10.0;
    processorRef.requestLevelReset(); //the mix gain starts again too
}

//Everything Chris's meter worked out goes to the display: the grades, the scores behind the tone colour, the tone colour itself
template <typename M> void PluginEditor::takeScores(const M& c)
{
    meter.outputR = c.outputR; meter.outputG = c.outputG; meter.outputB = c.outputB;
    meter.outputVol = c.outputVol; meter.outputMin = c.outputMin; meter.outputMax = c.outputMax; meter.gradientMin = c.gradientMin;
    meter.storeR = c.storeR; meter.storeG = c.storeG; meter.storeB = c.storeB; meter.cyanFix = c.cyanFix;
    meter.backdropColour = c.backdropColour;
    meter.peakPrevGrade = c.peakPrevGrade; meter.slewPrevGrade = c.slewPrevGrade; meter.bassPrevGrade = c.bassPrevGrade;
    meter.peakGrade = c.peakGrade; meter.slewGrade = c.slewGrade; meter.bassGrade = c.bassGrade;
    meter.cumulative = c.cumulative; meter.duration = c.duration; meter.cumulativePlus = c.cumulativePlus; meter.durationPlus = c.durationPlus;
    meter.totalPackage = c.totalPackage; meter.bonusScore = c.bonusScore;
    meter.authority = c.authority; meter.power = c.power; meter.detail = c.detail;
}

void PluginEditor::applyGradeLab()
{
    decltype(chrisTunedMeter.mdP) p; //the original values
    const bool sameVersion = gradeLab.version == juce::String(chris::kId);
    bool differs = false;
    if (gradeLab.fromFile && sameVersion && !meter.gradeLabDisabled) {
        std::map<juce::String, double*> field = {{"decayPeak", &p.decayPeak}, {"decaySlew", &p.decaySlew}, {"decayBass", &p.decayBass}, {"binCap", &p.binCap}, {"curvePS", &p.curvePS},
            {"curveBass", &p.curveBass}, {"gradeExp", &p.gradeExp}, {"gradeRise", &p.gradeRise}, {"balanceExp", &p.balanceExp}, {"letterExp", &p.letterExp}, {"clipLevel", &p.clipLevel},
            {"clipDecay", &p.clipDecay}, {"clipDecayBoth", &p.clipDecayBoth}, {"clipWeight", &p.clipWeight}, {"blackLine", &p.blackLine}, {"toneDecay", &p.toneDecay}};
        for (const char* k : chrisTuned::kGradeLabKeys) { //only the constants this version's code has
            auto v = gradeLab.values.find(k); auto f = field.find(k);
            if (v == gradeLab.values.end() || f == field.end() || !std::isfinite(v->second)) continue;
            if (std::abs(v->second - *f->second) > 1.0e-12) differs = true;
            *f->second = v->second;
        }
    }
    chrisTunedMeter.mdP = p;
    gradeLabActive = differs;
    meter.gradeLabState = !gradeLab.fromFile ? 0 : (!sameVersion ? 3 : (meter.gradeLabDisabled ? 2 : (differs ? 1 : 0)));
    gradeLabDisabledSeen = meter.gradeLabDisabled;
    meter.repaint();
}

void PluginEditor::idle()
{
    if (++gradeLabCounter >= 30 || meter.gradeLabDisabled != gradeLabDisabledSeen) { //about once a second: gradeLAB settings exported from the Analyser take effect live
        const bool clicked = meter.gradeLabDisabled != gradeLabDisabledSeen;
        gradeLabCounter = 0;
        if (gradeLab.reloadIfChanged() || clicked) applyGradeLab();
    }
    //MESSAGES FROM CHRIS'S AUDIO LOOP. Each goes to his own meter exactly as his editor handles it, and its values to our display.
    //At the end of each window his meter runs its scoring once (its paint, with drawing switched off): once per window, never per
    //screen redraw, so the grades do not depend on how often the screen is redrawn or how fast the host plays.
    chris::PluginProcessor::AudioToUIMessage msg;
    bool repaintTS{false}; //we don't redraw interface just for getting data into the GUI section
    //At most a few windows per screen tick (real time needs one or two): if the host renders faster than real time, the rest wait in
    //the queue (and, as in Chris's plugin, windows beyond its size are skipped), so the screen and the host never stall.
    int windowsThisTick = 0;
    while (windowsThisTick < 6 && processorRef.chrisAudio.audioToUI.pop(msg)) {
        bool chrisRepaint = false;
        chris::dispatch(chrisMeter, msg, chrisRepaint);
        chrisTuned::dispatch(chrisTunedMeter, msg, chrisRepaint); //the tuned copy always runs alongside, so ticking "grade" mid-track has the whole history
        const juce::String what((int)msg.what >= 0 && (int)msg.what < chris::kNumWhats ? chris::kWhatNames[(int)msg.what] : "");
        if (what == "INCREMENT") {
            chrisMeter.paint(noDraw); //his scoring for this window
            const bool gradeTick = meter.tune[12] > 0.5f;
            chrisTunedMeter.mdDecay = gradeTick ? meter.tune[6] : 0.0f; chrisTunedMeter.mdGate = gradeTick ? meter.tune[7] : 0.0f; //the Detail settings only reach the grade when "grade" is ticked
            chrisTunedMeter.paint(noDraw);
            //"grade" ticked and a Detail setting above 0: the grade and tone colours come from the tuned copy. Otherwise they are exactly Chris's.
            meter.gradeTuned = (gradeTick && (meter.tune[6] > 0.0f || meter.tune[7] > 0.0f)) || gradeLabActive; //or gradeLAB's constants are in use
            if (meter.gradeTuned) takeScores(chrisTunedMeter); else takeScores(chrisMeter);
            meter.levelMode = meter.tune[14] > 0.75f ? 2 : (meter.tune[14] > 0.25f ? 1 : 0);
            meter.pushIncrement(); repaintTS = true;
            ++windowsThisTick;
            continue;
        }
        //meterDARK's display shows the REAL level: take the level gain (normalised / mix modes) off again. Peaks are square-rooted amplitudes, slews and the beam/departure
        //lines scale with amplitude, zero-cross lengths do not change.
        const double gn = msg.mdGain > 0.0f ? (double)msg.mdGain : 1.0;
        float v = msg.newValue;
        if (gn != 1.0) {
            if (what.startsWith("PEAK")) v = (float)(v/std::sqrt(gn));
            else if (what.startsWith("SLEW") || what.startsWith("BSLW") || what.startsWith("DSLW") || what.startsWith("BEAM") || what.startsWith("DEPT")
                     || what.startsWith("BRMS") || what.startsWith("DRMS")) v = (float)(v/gn);
        }
        if (what == "PEAK_LEFT") meter.pushPeakL(v); else if (what == "PEAK_RIGHT") meter.pushPeakR(v);
        else if (what == "SLEW_LEFT") meter.pushSlewL(v); else if (what == "SLEW_RIGHT") meter.pushSlewR(v);
        else if (what == "ZERO_LEFT") meter.pushZeroL(v); else if (what == "ZERO_RIGHT") meter.pushZeroR(v);
        else if (what == "BEAM_LEFT" || what == "BSLW_LEFT") meter.pushBeamL(v); else if (what == "BEAM_RIGHT" || what == "BSLW_RIGHT") meter.pushBeamR(v);
        else if (what == "DEPT_LEFT" || what == "DSLW_LEFT") meter.pushDeptL(v); else if (what == "DEPT_RIGHT" || what == "DSLW_RIGHT") meter.pushDeptR(v);
        else meter.pushExtra(what, v); //newer lines (0.5.6: the zero cross and RMS lines)
    }
    processorRef.chrisAudio.audioPlaying = meter.meterPlaying; //clicking the meter pauses it, as in Chris's plugin
    if (meter.peakTrimDb != lastPeakTrimSeen) {
        processorRef.peakTrimDb = lastPeakTrimSeen = meter.peakTrimDb;
    } else if (processorRef.peakTrimDb != lastPeakTrimSeen) {
        meter.peakTrimDb = lastPeakTrimSeen = processorRef.peakTrimDb;
        meter.repaint();
    }
    if (meter.slewTrimDb != lastSlewTrimSeen) {
        processorRef.slewTrimDb = lastSlewTrimSeen = meter.slewTrimDb; //the user dragged the slew trim
    } else if (processorRef.slewTrimDb != lastSlewTrimSeen) {
        meter.slewTrimDb = lastSlewTrimSeen = processorRef.slewTrimDb; //a project or preset loaded a different one
        meter.repaint();
    }
    for (int i = 0; i < kTuneCount; ++i) { //hint tuning: same two-way sync as the trims
        if (meter.tune[i] != lastTuneSeen[i]) processorRef.hintTune[i] = lastTuneSeen[i] = meter.tune[i];
        else if (processorRef.hintTune[i] != lastTuneSeen[i]) {meter.tune[i] = lastTuneSeen[i] = processorRef.hintTune[i]; meter.repaint();}
    }
    if (repaintTS) {
        if (airwindowsLookAndFeel.useToneColor && meter.backdropColour.operator!=(meter.cachedColour)) {
            repaint();
            meter.cachedColour = meter.backdropColour;
            //we're going to maybe NOT constantly repaint everything, even in tonecolor mode
        } else {
            meter.repaint();
        }
        //normally it's the meter we have to update without touching it with the mouse,
        //but if the whole plugin has to be tone color, it all must repaint.
    }
}
