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
    for (int i = 0; i < 12; ++i) {meter.tune[i] = lastTuneSeen[i] = processorRef.hintTune[i]; meter.tuneUser[i] = processorRef.userTune[i];}
    meter.onSaveDefault = [this]() {
        processorRef.saveUserDefaults(meter.tune, meter.peakTrimDb, meter.slewTrimDb);
        for (int i = 0; i < 12; ++i) meter.tuneUser[i] = processorRef.userTune[i];
    };
    meter.setOpaque(true);
    meter.resetArrays();
    meter.addMouseListener(this, false);
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
}

//Everything Chris's meter worked out goes to the display: the grades, the scores behind the tone colour, the tone colour itself
void PluginEditor::takeScores()
{
    const auto& c = chrisMeter;
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

void PluginEditor::idle()
{
    //MESSAGES FROM CHRIS'S AUDIO LOOP. Each goes to his own meter exactly as his editor handles it, and its values to our display.
    //At the end of each window his meter runs its scoring once (its paint, with drawing switched off): once per window, never per
    //screen redraw, so the grades do not depend on how often the screen is redrawn or how fast the host plays.
    chris::PluginProcessor::AudioToUIMessage msg;
    bool repaintTS{false}; //we don't redraw interface just for getting data into the GUI section
    while (processorRef.chrisAudio.audioToUI.pop(msg)) {
        bool chrisRepaint = false;
        chris::dispatch(chrisMeter, msg, chrisRepaint);
        const juce::String what((int)msg.what >= 0 && (int)msg.what < chris::kNumWhats ? chris::kWhatNames[(int)msg.what] : "");
        if (what == "INCREMENT") {
            chrisMeter.paint(noDraw); //his scoring for this window
            takeScores();
            meter.pushIncrement(); repaintTS = true;
            continue;
        }
        const float v = msg.newValue;
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
    for (int i = 0; i < 12; ++i) { //hint tuning: same two-way sync as the trims
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
